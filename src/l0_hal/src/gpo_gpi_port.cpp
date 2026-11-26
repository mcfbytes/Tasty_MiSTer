// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/gpo_gpi_port.h"

#include <chrono>
#include <ctime>
#include <new>
#include <type_traits>
#include <utility>

#include "infra/xthread/rt_stats.h"
#include "infra/prelude.h"
#include "hal/gpo_gpi_pair.h"
#include "os/raw_monotonic_clock.h"

namespace mister::hal {

LinkLook look(GpoWord gpo_register, GpiWord gpi) noexcept {
    if (!gpo_register.contains(gpo::kCoreEnable))
        return LinkLook::IdView;
    const LwPresence lw = LwPresence::of(gpi);
    if (!lw.in_user_mode() || !lw.present() || !lw.enabled())
        return LinkLook::NoLwHost;
    return LinkLook::LwHostEnabled;
}

LinkLook GpoGpiPort::look_at_open_() noexcept {
    const GpoWord gpo_now{gpo_reg_.read(GpoBlock::Reg::Gpo)};

    const GpiWord gpi_now = gpo_now.contains(gpo::kCoreEnable) ? read_gpi() : GpiWord{};
    return hal::look(gpo_now, gpi_now);
}

void GpoGpiPort::store_gpo(GpoWord v) noexcept {
    v.write_to(gpo_reg_);
    if (obs_)
        obs_->on_store(v);
}

void GpoGpiPort::commit_gpo(GpoWord v) noexcept {
    gpo_ = v;
    store_gpo(v);
}

GpiWord GpoGpiPort::read_gpi() const noexcept {
    return GpiWord{gpi_reg_.read(GpiBlock::Reg::Gpi)};
}

Ex<GpiWord> GpoGpiPort::spin_ack(AckLevel want, Timeout on_timeout) noexcept {

    unsigned budget = ack_soft_spins_;
    os::RawMonotonicClock::time_point limit{};
    bool armed = false;
    for (;;) {
        GpiWord gpi = read_gpi();
        if (gpi.test(kGpiNotReadyField)) {

            return std::unexpected(gpi.refusal(Errc::fpga_not_ready, ERR_SITE()));
        }

        if (gpi.test(kGpiAckField) == (want == AckLevel::Raised))
            return gpi;
        if (--budget != 0u)
            continue;
        budget = ack_soft_spins_;
        const os::RawMonotonicClock::time_point now = os::RawMonotonicClock::now();
        if (!armed) {
            limit = now + ack_timeout_ns_;
            armed = true;
            continue;
        }
        if (now >= limit) {
            if (on_timeout == Timeout::Counted && stats_)
                stats_->spin_timeouts.add();
            return std::unexpected(gpi.refusal(Errc::spi_timeout, ERR_SITE()));
        }
    }
}

Ex<GpoGpiPortOpen> GpoGpiPort::open(
    GpoGpiPair mailbox,
    LookPolicy policy,
    xthread::RtStats* stats
) {

    if (!mailbox.held())
        return std::unexpected(Error{Errc::busy, ERR_SITE(), 0});
    const LinkTimingValues& timing = mailbox.timing();

    GpoGpiPort bus;
    bus.gpo_reg_ = mailbox.gpo();
    bus.gpi_reg_ = mailbox.gpi();
    bus.stats_ = infra::opt_ref_of(stats);
    bus.ack_soft_spins_ = timing.ack_soft_spins;
    bus.ack_timeout_ns_ = timing.ack_timeout_ns;

    bus.mailbox_ = std::move(mailbox).consume();

    const LinkLook look = bus.look_at_open_();
    if (look == LinkLook::LwHostEnabled && policy == LookPolicy::StopOnHost) {
        return std::unexpected(Error{Errc::busy, ERR_SITE(), 0});
    }

    bus.commit_gpo(GpoWord{});
    return GpoGpiPortOpen{.bus = std::move(bus), .look = look};
}

Ex<GpoGpiPortOpen> GpoGpiPort::attach_registers(
    BlockView<GpoBlock> gpo,
    BlockView<GpiBlock> gpi,
    IGpoObserver* obs,
    const LinkTimingValues& timing,
    LookPolicy policy,
    xthread::RtStats* stats
) noexcept {
    if (!gpo.bound() || !gpi.bound()) {
        fatal(Error{Errc::io, ERR_SITE(), 0}, "GpoGpiPort::attach_registers");
    }
    GpoGpiPort bus;
    bus.gpo_reg_ = gpo;
    bus.gpi_reg_ = gpi;
    bus.obs_ = infra::opt_ref_of(obs);
    bus.stats_ = infra::opt_ref_of(stats);
    bus.ack_soft_spins_ = timing.ack_soft_spins;
    bus.ack_timeout_ns_ = timing.ack_timeout_ns;

    const LinkLook look = bus.look_at_open_();
    if (look == LinkLook::LwHostEnabled && policy == LookPolicy::StopOnHost) {
        return std::unexpected(Error{Errc::busy, ERR_SITE(), 0});
    }
    bus.commit_gpo(GpoWord{});
    return GpoGpiPortOpen{.bus = std::move(bus), .look = look};
}

bool GpoGpiPort::ready() const {
    TASTY_SEAT_BODY(GpoGpiPort);

    return !read_gpi().test(kGpiNotReadyField);
}

PinSample GpoGpiPort::sample() const noexcept {
    return decode_sample(read_gpi());
}

Ex<CoreTypeByte> GpoGpiPort::identify() {

    const GpiWord pre = read_gpi();
    if (pre.test(kGpiNotReadyField)) {
        return std::unexpected(pre.refusal(Errc::fpga_not_ready, ERR_SITE()));
    }

    const GpoWord id_view = gpo_.without(gpo::kCoreEnable);
    commit_gpo(id_view);
    const GpiWord sample = read_gpi();
    commit_gpo(id_view.with(gpo::kCoreEnable));

    if (sample.field(kGpiCoreIdField) != kCoreIdMagic) {
        return std::unexpected(sample.refusal(Errc::negotiation, ERR_SITE()));
    }
    const CoreTypeByte type{static_cast<std::uint8_t>(sample.field(kGpiTypeByteField))};
    return type;
}

CoreCapabilities GpoGpiPort::latch_capabilities() {

    if (!gpo_.contains(gpo::kCoreEnable)) {
        fatal(
            gpo_.refusal(Errc::negotiation, ERR_SITE()),
            "GpoGpiPort::latch_capabilities() sampled with core-enable low"
        );
    }

    const GpiWord gpi = read_gpi();
    CoreCapabilities caps{};
    caps.width = gpi.test(kGpiWidthField) ? Width::Word : Width::Byte;
    caps.io_version = IoVersion{static_cast<std::uint8_t>(gpi.field(kGpiIoVersionField))};
    caps.board_digital = gpi.test(kGpiBoardDigitalField);
    caps.button_osd = gpi.test(kGpiButtonOsdField);
    caps.button_user = gpi.test(kGpiButtonUserField);
    caps.free_caps = static_cast<std::uint8_t>(gpi.field(kGpiFreeCapsField));
    caps.lw = LwPresence::of(gpi);
    caps.ocram = OcramPresence::of(gpi);

    width_ = caps.width;
    io_version_ = caps.io_version;
    return caps;
}

void GpoGpiPort::set_core_reset(bool asserted) {

    const GpoWord
        base = gpo_.without(gpo::kCoreReset).without(gpo::kCoreEnable).without(gpo::kOcramRun);
    commit_gpo(base.with(asserted ? gpo::kCoreReset : gpo::kCoreEnable));
}

void GpoGpiPort::clear_signals() {
    commit_gpo(GpoWord{});
}

void GpoGpiPort::select(ChipSelect cs) {

    GpoWord v = gpo_.with(gpo::kCoreEnable).without(gpo::kCsMask);
    switch (cs) {
        case ChipSelect::Fpga:
            v = v.with(gpo::kFpgaEnable);
            break;
        case ChipSelect::Osd:
            v = v.with(gpo::kOsdEnable);
            break;
        case ChipSelect::Io:
            v = v.with(gpo::kIoEnable);
            break;
        case ChipSelect::None:
            break;

        case ChipSelect::OsdVga:
            v = v.with(gpo::kOsdEnable).with(gpo::kFpgaEnable);
            break;
        case ChipSelect::OsdHdmi:
            v = v.with(gpo::kOsdEnable).with(gpo::kIoEnable);
            break;
    }
    commit_gpo(v);
}

void GpoGpiPort::deselect() {
    commit_gpo(gpo_.with(gpo::kCoreEnable).without(gpo::kCsMask));
}

Ex<void> GpoGpiPort::refuse_if_posted_() const noexcept {
    if (post_ == Post::None)
        return {};
    return std::unexpected(Error{Errc::would_block, ERR_SITE(), static_cast<std::uint32_t>(post_)});
}

Ex<LinkWord> GpoGpiPort::transfer(LinkWord out) {
    if (auto held = refuse_if_posted_(); !held)
        return std::unexpected(held.error());
    return handshake_(out, false);
}

Ex<bool> GpoGpiPort::transfer_or_post(LinkWord out) {
    TASTY_SEAT_BODY(GpoGpiPort);
    if (auto held = refuse_if_posted_(); !held)
        return std::unexpected(held.error());
    auto r = handshake_(out, true);
    if (r)
        return true;
    if (post_ != Post::None)
        return false;
    return std::unexpected(r.error());
}

Ex<LinkWord> GpoGpiPort::handshake_(LinkWord out, bool guarded) noexcept {

    const GpoWord gpo = gpo_.without(gpo::kData).without(gpo::kStrobe).with_data(out);
    commit_gpo(gpo);
    commit_gpo(gpo.with(gpo::kStrobe));
    if (auto ack = spin_ack(AckLevel::Raised, guarded ? Timeout::Guarded : Timeout::Counted);
        !ack) {
        if (guarded && ack.error().code == Errc::spi_timeout) {
            post_ = Post::Rising;
            return std::unexpected(ack.error());
        }

        commit_gpo(gpo);
        return std::unexpected(ack.error());
    }
    commit_gpo(gpo);
    const auto sample = spin_ack(AckLevel::Dropped, guarded ? Timeout::Guarded : Timeout::Counted);
    if (!sample) {
        if (guarded && sample.error().code == Errc::spi_timeout)
            post_ = Post::Falling;
        return std::unexpected(sample.error());
    }

    return sample->data();
}

Ex<void> GpoGpiPort::post(LinkWord out) {
    TASTY_SEAT_BODY(GpoGpiPort);
    if (auto held = refuse_if_posted_(); !held)
        return held;
    const GpoWord gpo = gpo_.without(gpo::kData).without(gpo::kStrobe).with_data(out);
    commit_gpo(gpo);
    commit_gpo(gpo.with(gpo::kStrobe));
    post_ = Post::Rising;
    return {};
}

Ex<bool> GpoGpiPort::posted_done() {
    TASTY_SEAT_BODY(GpoGpiPort);
    if (post_ == Post::None)
        return true;
    GpiWord gpi = read_gpi();
    if (post_ == Post::Rising && !gpi.test(kGpiNotReadyField)) {
        if (!gpi.test(kGpiAckField))
            return false;
        commit_gpo(gpo_.without(gpo::kStrobe));
        post_ = Post::Falling;
        gpi = read_gpi();
    }
    if (gpi.test(kGpiNotReadyField)) {
        abandon_post();
        return std::unexpected(gpi.refusal(Errc::fpga_not_ready, ERR_SITE()));
    }
    if (gpi.test(kGpiAckField))
        return false;
    post_ = Post::None;
    return true;
}

void GpoGpiPort::abandon_post() noexcept {
    TASTY_SEAT_BODY(GpoGpiPort);
    if (post_ == Post::None)
        return;
    commit_gpo(gpo_.without(gpo::kStrobe));
    post_ = Post::None;
}

Ex<void> GpoGpiPort::block_write(std::span<const std::uint8_t> data) {
    if (auto held = refuse_if_posted_(); !held)
        return held;
    const GpoWord base = gpo_.without(gpo::kData).without(gpo::kStrobe);
    GpoWord gpo = base;
    if (width_ == Width::Word) {
        const std::size_t n = data.size() / 2u;
        for (std::size_t i = 0; i < n; ++i) {
            const auto word = static_cast<std::uint16_t>(
                static_cast<std::uint32_t>(data[2u * i])
                | (static_cast<std::uint32_t>(data[2u * i + 1u]) << 8)
            );
            gpo = base.with_data(LinkWord{word});
            store_gpo(gpo);
            store_gpo(gpo.with(gpo::kStrobe));
        }
    } else {
        for (const std::uint8_t byte : data) {
            gpo = base.with_data(LinkWord{byte});
            store_gpo(gpo);
            store_gpo(gpo.with(gpo::kStrobe));
        }
    }

    commit_gpo(gpo);
    return {};
}

Ex<void> GpoGpiPort::block_read(std::span<std::uint8_t> data) {
    if (auto held = refuse_if_posted_(); !held)
        return held;
    const GpoWord gpo = gpo_.without(gpo::kData).without(gpo::kStrobe);
    const std::size_t n = (width_ == Width::Word) ? data.size() / 2u : data.size();
    if (n == 0)
        return {};

    if (width_ == Width::Word) {
        for (std::size_t i = 0; i < n; ++i) {
            store_gpo(gpo.with(gpo::kStrobe));
            store_gpo(gpo);
            const LinkWord w = read_gpi().data();
            data[2u * i] = w.low_byte();
            data[2u * i + 1u] = w.high_byte();
        }
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            store_gpo(gpo.with(gpo::kStrobe));
            store_gpo(gpo);
            read_gpi().data().store_low(data.subspan(i).first<1>());
        }
    }

    gpo_ = gpo;
    return {};
}

Ex<ButtonMask> GpoGpiPort::buttons() {
    commit_gpo(gpo_.with(gpo::kCoreEnable));
    const GpiWord gpi = read_gpi();
    if (gpi.test(kGpiNotReadyField)) {

        return std::unexpected(gpi.refusal(Errc::fpga_not_ready, ERR_SITE()));
    }
    return gpi.buttons();
}

void GpoGpiPort::set_disk_led(bool on) {
    commit_gpo(on ? gpo_.with(gpo::kDiskLed) : gpo_.without(gpo::kDiskLed));
}

void GpoGpiPort::ocram_run(OcramRunEdge edge) {
    TASTY_SEAT_BODY(GpoGpiPort);
    switch (edge) {
        case OcramRunEdge::Raise:
            commit_gpo(gpo_.without(gpo::kOcramRun));
            commit_gpo(gpo_.with(gpo::kOcramRun));
            return;
        case OcramRunEdge::Drop:
            commit_gpo(gpo_.without(gpo::kOcramRun));
            return;
    }
}

}
