// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/adv7513_seat.h"

#include <utility>

#include "app/video_wire.h"
#include "svc/video_service.h"

namespace mister::app {

Adv7513Seat::Adv7513Seat(
    const os::IClock& clock,
    const hal::VideoOutDecl& board,
    infra::OptRef<svc::adv7513::II2cAdapter> i2c,
    const hal::IHdmiInterrupt& hdmi_int
) noexcept
    : clock_(clock),
      board_(board),
      hdmi_int_(hdmi_int),
      adapter_(i2c ? *i2c : i2c_backend_),
      breaker_(adapter_),
      main_map_(breaker_, svc::adv7513::kMainAddr),
      edid_map_(breaker_, svc::adv7513::kEdidAddr),
      cec_map_(breaker_, svc::adv7513::kCecAddr) {}

bool Adv7513Seat::hdmi_int_asserted() const {
    TASTY_SEAT_BODY(Adv7513Seat);
    return hdmi_int_.hdmi_int_asserted();
}

svc::adv7513::Io Adv7513Seat::io() noexcept {
    svc::adv7513::Io io{};
    io.main = infra::OptRef<svc::adv7513::IRegMap>{main_map_};
    io.edid = infra::OptRef<svc::adv7513::IRegMap>{edid_map_};
    io.cec = infra::OptRef<svc::adv7513::IRegMap>{cec_map_};
    io.hdmi = infra::OptRef<const hal::IHdmiInterrupt>{*this};
    io.delay = infra::OptRef<os::IDelay>{delay_};
    io.clock = infra::OptRef<const os::IClock>{clock_};
    return io;
}

void Adv7513Seat::on_core_loaded(Host h) {
    init_pending_ = true;
    ints_armed_ = false;
    prelude_done_seen_ = h.wire.prelude_stats().completions;
    h.video.set_hdmi_capable(false);
    hpd_gen_seen_ = h.video.hotplug_generation().v;
    audio_gen_seen_ = h.video.audio_generation().v;
}

Adv7513Seat::Edge Adv7513Seat::hotplug(Host h, const svc::adv7513::InitOptions& init) {
    if (!adv_)
        return Edge::None;

    if (!ints_armed_) {
        const auto ps = h.wire.prelude_stats();
        if (ps.completions == prelude_done_seen_)
            return Edge::None;
        ints_armed_ = true;
        const bool has_int = ps.hdmi_int != 0;
        h.video.set_hdmi_capable(has_int);
        if (has_int) {
            if (auto r = adv_->arm_interrupts(true); !r)
                h.last_error = r.error();
        }
    }
    if (!h.video.hdmi_capable())
        return Edge::None;

    const std::int64_t now = clock_.now().count();
    if (now < next_hpd_ns_)
        return Edge::None;
    next_hpd_ns_ = now + 100'000'000;
    const std::uint32_t failed_before = breaker_.failures();
    if (auto r = h.video.poll_hotplug(); !r)
        h.last_error = r.error();
    h.stats.i2c_poll_errors += breaker_.failures() - failed_before;

    Edge out = Edge::None;
    if (const auto g = h.video.hotplug_generation().v; g != hpd_gen_seen_) {
        hpd_gen_seen_ = g;
        init_pending_ = true;
        out = Edge::Owed;
    }
    if (const auto g = h.video.audio_generation().v; g != audio_gen_seen_) {
        audio_gen_seen_ = g;
        if (auto r = adv_->configure_audio(init); !r) {
            h.last_error = r.error();
        }
    }
    return out;
}

Adv7513Seat::Edge Adv7513Seat::configure(
    Host h,
    const svc::adv7513::InitOptions& init,
    const Spd& spd
) {
    if (!probed_) {
        probed_ = true;
        if (!attach(h))
            return Edge::None;
    }
    const bool want_init = init_pending_;
    init_pending_ = false;

    if (want_init)
        h.video.clear_init_owed();

    if (!adv_)
        return Edge::None;

    if (!want_init)
        return Edge::None;

    svc::adv7513::InitOptions opts = init;
    opts.has_hdmi_int = h.video.hdmi_capable();
    const auto r = adv_->configure(opts);
    sync_counters(h.stats);
    if (!r) {
        h.last_error = r.error();
        init_ok_ = false;
        h.stats.i2c = VideoI2cOutcome::Failed;
        return Edge::Lost;
    }
    init_ok_ = true;
    h.stats.i2c = VideoI2cOutcome::Configured;

    send_spd(h, spd);
    return Edge::None;
}

bool Adv7513Seat::attach(Host h) {
    breaker_.reset();
    auto b = svc::adv7513::Adv7513Bus::attach(breaker_, board_, probe_);
    h.stats.i2c_bus = probe_.bus;
    if (!b) {
        h.last_error = b.error();
        h.stats.i2c = probe_.presence == svc::adv7513::Presence::Ambiguous
            ? VideoI2cOutcome::Ambiguous
            : VideoI2cOutcome::Absent;

        breaker_.hold();
        schedule_reprobe();
        return false;
    }
    adv_.emplace(std::move(*b));
    h.stats.i2c = VideoI2cOutcome::Present;
    attached_ns_ = clock_.now().count();

    h.video.attach();
    return true;
}

void Adv7513Seat::schedule_reprobe() noexcept {
    next_probe_ns_ = clock_.now().count()
        + static_cast<std::int64_t>(probe_backoff_ms_) * 1'000'000;
    probe_backoff_ms_ = probe_backoff_ms_ >= kReprobeMaxMs / 2 ? kReprobeMaxMs
                                                               : probe_backoff_ms_ * 2;
}

Adv7513Seat::Edge Adv7513Seat::recover(Host h) {
    if (!probed_)
        return Edge::None;
    if (adv_) {
        if (!breaker_.tripped())
            return Edge::None;
        sync_counters(h.stats);
        writes_base_ = h.stats.i2c_writes;
        errors_base_ = h.stats.i2c_errors;
        adv_.reset();
        init_ok_ = false;
        h.stats.i2c = VideoI2cOutcome::Failed;

        if (clock_.now().count() - attached_ns_ >= std::int64_t{kReprobeMaxMs} * 1'000'000)
            probe_backoff_ms_ = kReprobeFirstMs;
        schedule_reprobe();
        return Edge::Lost;
    }
    if (clock_.now().count() < next_probe_ns_)
        return Edge::None;
    ++h.stats.i2c_reprobes;
    if (!attach(h))
        return Edge::None;
    init_pending_ = true;
    return Edge::Owed;
}

void Adv7513Seat::sync_counters(VideoPumpStats& stats) noexcept {
    if (!adv_)
        return;
    stats.i2c_writes = writes_base_ + adv_->writes();
    stats.i2c_errors = errors_base_ + adv_->errors();
    if (stats.i2c_fail_reg == 0)
        stats.i2c_fail_reg = adv_->fail_reg();
    if (stats.spd_fail_reg == 0)
        stats.spd_fail_reg = adv_->spd_fail_reg();
}

void Adv7513Seat::report(VideoPumpStats& stats) noexcept {
    sync_counters(stats);
    const auto& f = breaker_.first();
    stats.i2c_first_op = f.op;
    stats.i2c_first_reg = f.reg;
    stats.i2c_first_errno = f.err;
    stats.i2c_last_errno = breaker_.last_errno();
    stats.i2c_trips = breaker_.trips();
    stats.i2c_breaker_open = breaker_.tripped();
}

void Adv7513Seat::send_spd(Host h, const Spd& spd) {
    if (!adv_)
        return;

    if (spd.direct_video) {
        ++h.stats.spd_skips_dv;
        return;
    }
    if (spd.quirk != 0)
        return;

    Ex<void> r{};
    if (!probe_.spd_ack) {
        r = adv_->spd_disable();
        if (r)
            ++h.stats.spd_disables;
    } else {
        const auto p = svc::adv7513::build_standard_spd(spd.core_name);
        if (spd.core_name.empty())
            ++h.stats.spd_noname;
        r = adv_->spd_config(p);
        if (r)
            ++h.stats.spd_sends;
    }

    sync_counters(h.stats);
    if (!r) {

        h.last_error = r.error();
    }
}

std::optional<VideoI2cOutcome> Adv7513Seat::program_mode(Host h, const svc::Modeline& m) {
    if (!adv_)
        return std::nullopt;
    auto r = adv_->set_mode(m, false);
    sync_counters(h.stats);
    if (!r) {
        h.last_error = r.error();
        return VideoI2cOutcome::Failed;
    }
    const std::uint32_t failed_before = breaker_.failures();
    auto v = adv_->verify(m, false);
    h.stats.i2c_verify_errors += breaker_.failures() - failed_before;
    if (!v) {
        h.last_error = v.error();
        return VideoI2cOutcome::Failed;
    }
    return v->matched ? VideoI2cOutcome::Verified : VideoI2cOutcome::Configured;
}

}
