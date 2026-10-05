// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/video_pump.h"
#include "app/hps_framebuffer.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <cerrno>
#include <ctime>
#include <utility>
#include <vector>

#include "svc/config.h"
#include "svc/config_parser.h"
#include "svc/filter_store.h"
#include "svc/vfs.h"

namespace mister::app {

namespace {

Ex<std::vector<std::byte>> read_all(const svc::Vfs& vfs, std::string_view path) {
    auto f = vfs.open(path, svc::OpenMode::ReadWhole);
    if (!f) return std::unexpected(f.error());
    auto sz = (*f)->size();
    if (!sz) return std::unexpected(sz.error());
    std::vector<std::byte> buf(static_cast<std::size_t>(sz->v));
    std::size_t off = 0;
    while (off < buf.size()) {
        auto n = (*f)->read_at(off, std::span<std::byte>(buf).subspan(off));
        if (!n) return std::unexpected(n.error());
        if (*n == 0) {
            return std::unexpected(
                Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(off)});
        }
        off += *n;
    }
    return buf;
}

std::uint32_t fpix_detail(double f) noexcept {
    if (!(f > 0.0) || !(f < 4.0e9)) return 0u;
    return static_cast<std::uint32_t>(f);
}

std::uint32_t fpix_khz(double mhz) noexcept {
    if (!(mhz > 0.0) || !(mhz < 4.0e6)) return 0u;
    return static_cast<std::uint32_t>(mhz * 1000.0 + 0.5);
}

}  // namespace

VideoPump::VideoPump(const svc::Vfs& vfs, const os::IClock& clock,
                     const hal::VideoOutDecl& board) noexcept
    : vfs_(&vfs), clock_(&clock), board_(board) {
    if (auto v = svc::VideoService::create()) {
        video_.emplace(std::move(*v));
        video_->set_wire_sink(wire_);

        video_->set_hdmi_capable(false);
        video_->set_resolution_sampler(wire_);
        publish_scaling_policy();
    }
    adapter_ = &i2c_backend_;
    rebind_sub_maps();
}

void VideoPump::set_i2c_adapter(svc::adv7513::II2cAdapter& a) noexcept {
    if (probed_) return;
    adapter_ = &a;
    breaker_.bind(a);
    rebind_sub_maps();
}

void VideoPump::rebind_sub_maps() noexcept {
    main_map_ = svc::adv7513::SubMap{breaker_, svc::adv7513::kMainAddr};
    edid_map_ = svc::adv7513::SubMap{breaker_, svc::adv7513::kEdidAddr};
    cec_map_ = svc::adv7513::SubMap{breaker_, svc::adv7513::kCecAddr};
}

void VideoPump::load_filters() {

    auto set = std::make_unique<svc::FilterSet>();
    const svc::ScalerSeeds seeds{vfilter_seeds_[0], vfilter_seeds_[1], vfilter_seeds_[2],
                                 vfilter_seeds_[3]};
    svc::load_filter_set(*vfs_, std::string_view{core_name_, core_name_len_}, seeds, *set);
    wire_.publish_filters(*set);
    filter_mode_ = set->modes[0];
    const bool enabled[4] = {set->modes[0] != 0, set->modes[1] != 0, set->modes[2] != 0,
                             set->modes[3] != 0};
    if (video_.has_value()) video_->set_filter_slots(std::span<const bool>(enabled, 4));
    ++stats_.filter_loads;
    publish_scaling_policy();
}

void VideoPump::latch_spec(std::string_view spec) noexcept {
    spec_len_ = static_cast<std::uint16_t>(spec.size() < kSpecMax ? spec.size() : kSpecMax - 1);
    std::memcpy(spec_, spec.data(), spec_len_);
    spec_[spec_len_] = '\0';
    spec_valid_ = true;
}

void VideoPump::force_vsync_adjust(std::uint8_t v) noexcept {
    if (v > 1) v = 0;
    vsync_force_ = v;
    vsync_adjust_ = v;
}

void VideoPump::arm_replay_ini(bool strict) noexcept {
    replay_ini_ = true;
    strict_direct_video_ = strict;
}

void VideoPump::set_mode_override(std::string_view spec) noexcept {
    latch_spec(spec);
    override_latched_ = true;
}

bool VideoPump::take_fb_cmd(std::string_view line) noexcept {
    TASTY_SEAT_BODY(VideoPump);
    if (hps_fb_ != nullptr) return hps_fb_->take_fb_cmd(line);
    const PublishOnExit publish_on_exit{this};
    ++stats_.fb_cmds_dropped;
    return true;
}

bool VideoPump::take_video_mode(std::string_view spec) noexcept {
    TASTY_SEAT_BODY(VideoPump);
    const PublishOnExit publish_on_exit{this};
    if (spec.empty()) {
        ++stats_.empty_specs;
        return true;
    }

    set_mode_override(spec);
    ++stats_.routed;
    pending_ = true;
    return true;
}

void VideoPump::on_core_loaded(bool front_end) {
    const PublishOnExit publish_on_exit{this};
    i2c_held_ = false;
    ++stats_.edges;
    pending_ = true;
    init_pending_ = true;
    ini_read_ = false;

    filters_pending_ = true;
    filter_mode_ = 0;

    families_valid_ = false;

    ints_armed_ = false;
    prelude_done_seen_ = wire_.prelude_stats().completions;
    if (video_.has_value()) {
        video_->set_hdmi_capable(false);
        hpd_gen_seen_ = video_->hotplug_generation().v;
        audio_gen_seen_ = video_->audio_generation().v;
    }
    front_end_ = front_end;
    if (video_.has_value()) video_->reset_resolution_gate();
    if (video_.has_value()) {
        geo_edges_ = 0;
        geo_gen_seen_ = video_->resolution_generation().v;
    }
    publish_scaling_policy();
}

void VideoPump::on_session_over() {
    const PublishOnExit publish_on_exit{this};
    if (pending_) {
        pending_ = false;
        filters_pending_ = false;
        ++stats_.abandoned;
    }
}

void VideoPump::tick() {
    const PublishOnExit publish_on_exit{this};

    if (i2c_held_) return;
    service_recovery();
    service_mode_phase();

    if (video_) {
        ++geo_polls_;
        if (const auto r = video_->poll_resolution(); !r) {
            if (r.error().code != Errc::not_found) last_error_ = r.error();
        }
        const auto g = video_->resolution_generation().v;
        if (g != geo_gen_seen_) {
            geo_gen_seen_ = g;
            ++geo_edges_;
            service_family_switch();
        }
        service_hotplug();
        service_idle_blank();
    }

    if (!pending_ || !video_) return;
    pending_ = false;
    if (const auto r = apply_now(); !r) last_error_ = r.error();
}

void VideoPump::publish_monitoring() noexcept {
    sync_bus_counters();
    const auto& f = breaker_.first();
    stats_.i2c_first_op = f.op;
    stats_.i2c_first_reg = f.reg;
    stats_.i2c_first_errno = f.err;
    stats_.i2c_last_errno = breaker_.last_errno();
    stats_.i2c_trips = breaker_.trips();
    stats_.i2c_breaker_open = breaker_.tripped();
    stats_cell_.publish(stats_);

    GeometryRecord g{};
    if (video_.has_value()) {
        const svc::VideoSample& s = video_->info();
        g.valid = geo_edges_ != 0u || s.res != 0u || s.width != 0u;
        g.fb_en = s.fb_en();
        g.rotated = s.rotated();
        g.res = s.res;
        g.width = s.width;
        g.height = s.height;
        g.vtime = s.vtime;
        g.core_seq = wire_.sampled_core_seq();
    }
    geo_cell_.publish(g);
}

void VideoPump::publish_scaling_policy() noexcept {
    svc::ScalingPolicy p{};
    p.scrw = scrw_;
    p.scrh = scrh_;
    p.vscale_mode = vscale_mode_;
    p.vscale_border = vscale_border_;
    p.filter_mode = filter_mode_;
    p.front_end = front_end_;
    wire_.publish_policy(p);
    wire_.publish_ar_custom(ar_cust_[0], ar_cust_[1], ar_cust_[2], ar_cust_[3]);
    wire_.publish_shadow_mask(0x0000);
}

void VideoPump::service_idle_blank() {
    if (!video_.has_value()) return;

    if (activity_ == nullptr) return;
    const auto want =
        idle_.tick(clock_->now().count(), activity_->activity_seq(), activity_->input_grabbed());
    if (!want.has_value()) return;
    if (auto r = video_->request_power(*want); !r) last_error_ = r.error();
}

bool VideoPump::hdmi_int_asserted() const {
    TASTY_SEAT_BODY(VideoPump);
    return hdmi_int_ != nullptr && hdmi_int_->hdmi_int_asserted();
}

void VideoPump::bind_adv7513_io() noexcept {
    if (!video_.has_value() || adapter_ == nullptr) return;
    svc::adv7513::Io io{};
    io.main = &main_map_;
    io.edid = &edid_map_;
    io.cec = &cec_map_;
    io.hdmi = this;
    io.delay = &delay_;
    io.clock = clock_;
    video_->set_adv7513_io(io);
}

void VideoPump::service_hotplug() {
    if (!adv_ || !video_.has_value()) return;

    if (!ints_armed_) {
        const auto ps = wire_.prelude_stats();
        if (ps.completions == prelude_done_seen_) return;
        ints_armed_ = true;
        const bool has_int = ps.hdmi_int != 0;
        video_->set_hdmi_capable(has_int);
        if (has_int) {
            if (auto r = adv_->arm_interrupts(true); !r) last_error_ = r.error();
        }
    }
    if (!video_->hdmi_capable()) return;

    const std::int64_t now = clock_->now().count();
    if (now < next_hpd_ns_) return;
    next_hpd_ns_ = now + 100'000'000;
    const std::uint32_t failed_before = breaker_.failures();
    if (auto r = video_->poll_hotplug(); !r) last_error_ = r.error();
    stats_.i2c_poll_errors += breaker_.failures() - failed_before;

    if (const auto g = video_->hotplug_generation().v; g != hpd_gen_seen_) {
        hpd_gen_seen_ = g;
        init_pending_ = true;
        pending_ = true;
    }
    if (const auto g = video_->audio_generation().v; g != audio_gen_seen_) {
        audio_gen_seen_ = g;
        if (auto r = adv_->configure_audio(init_opts_); !r) {
            last_error_ = r.error();
        }
    }
}

void VideoPump::configure_transmitter() {
    if (adapter_ == nullptr) return;

    if (!probed_) {
        probed_ = true;
        if (!attach_transmitter()) return;
    }
    const bool want_init = init_pending_;
    init_pending_ = false;

    if (want_init && video_.has_value()) video_->clear_init_owed();

    if (!adv_) return;

    if (!want_init) return;

    svc::adv7513::InitOptions opts = init_opts_;
    opts.has_hdmi_int = video_.has_value() && video_->hdmi_capable();
    const auto r = adv_->configure(opts);
    sync_bus_counters();
    if (!r) {
        last_error_ = r.error();
        init_ok_ = false;
        mode_pending_ = false;
        stats_.i2c = I2cOutcome::Failed;
        return;
    }
    init_ok_ = true;
    stats_.i2c = I2cOutcome::Configured;

    send_spd_packet();
}

bool VideoPump::attach_transmitter() {
    breaker_.reset();
    auto b = svc::adv7513::Adv7513Bus::attach(breaker_, board_, probe_);
    stats_.i2c_bus = probe_.bus;
    if (!b) {
        last_error_ = b.error();
        stats_.i2c = probe_.presence == svc::adv7513::Presence::Ambiguous ? I2cOutcome::Ambiguous
                                                                          : I2cOutcome::Absent;

        breaker_.hold();
        schedule_reprobe();
        return false;
    }
    adv_.emplace(std::move(*b));
    stats_.i2c = I2cOutcome::Present;
    attached_ns_ = clock_->now().count();

    bind_adv7513_io();
    return true;
}

void VideoPump::schedule_reprobe() noexcept {
    next_probe_ns_ =
        clock_->now().count() + static_cast<std::int64_t>(probe_backoff_ms_) * 1'000'000;
    probe_backoff_ms_ =
        probe_backoff_ms_ >= kReprobeMaxMs / 2 ? kReprobeMaxMs : probe_backoff_ms_ * 2;
}

void VideoPump::service_recovery() {
    if (i2c_held_) return;
    if (!probed_) return;
    if (adv_) {
        if (!breaker_.tripped()) return;
        sync_bus_counters();
        writes_base_ = stats_.i2c_writes;
        errors_base_ = stats_.i2c_errors;
        adv_.reset();
        init_ok_ = false;
        mode_pending_ = false;
        stats_.i2c = I2cOutcome::Failed;

        if (clock_->now().count() - attached_ns_ >= std::int64_t{kReprobeMaxMs} * 1'000'000)
            probe_backoff_ms_ = kReprobeFirstMs;
        schedule_reprobe();
        return;
    }
    if (clock_->now().count() < next_probe_ns_) return;
    ++stats_.i2c_reprobes;
    if (!attach_transmitter()) return;
    init_pending_ = true;
    pending_ = true;
}

void VideoPump::sync_bus_counters() noexcept {
    if (!adv_) return;
    stats_.i2c_writes = writes_base_ + adv_->writes();
    stats_.i2c_errors = errors_base_ + adv_->errors();
    if (stats_.i2c_fail_reg == 0) stats_.i2c_fail_reg = adv_->fail_reg();
    if (stats_.spd_fail_reg == 0) stats_.spd_fail_reg = adv_->spd_fail_reg();
}

void VideoPump::send_spd_packet() {
    if (!adv_) return;

    if (spd_direct_video_) {
        ++stats_.spd_skips_dv;
        return;
    }
    if (spd_quirk_ != 0) return;

    Ex<void> r{};
    if (!probe_.spd_ack) {
        r = adv_->spd_disable();
        if (r) ++stats_.spd_disables;
    } else {
        const auto p =
            svc::adv7513::build_standard_spd(std::string_view{core_name_, core_name_len_});
        if (core_name_len_ == 0) ++stats_.spd_noname;
        r = adv_->spd_config(p);
        if (r) ++stats_.spd_sends;
    }

    sync_bus_counters();
    if (!r) {

        last_error_ = r.error();
    }
}

void VideoPump::set_core_name(std::string_view name) noexcept {
    constexpr std::size_t kCap = sizeof(core_name_) - 1;
    const std::size_t n = name.size() < kCap ? name.size() : kCap;
    for (std::size_t i = 0; i < n; ++i)
        core_name_[i] = name[i];
    core_name_[n] = '\0';
    core_name_len_ = static_cast<std::uint8_t>(n);
}

void VideoPump::note_i2c(I2cOutcome o) noexcept {
    if (!init_ok_ && stats_.i2c == I2cOutcome::Failed && o != I2cOutcome::Failed) {
        return;
    }
    stats_.i2c = o;
}

void VideoPump::service_mode_phase() {
    if (!mode_pending_) return;

    const std::uint32_t emitted = wire_.stats().emitted;
    if (emitted > emit_watermark_) {
        mode_pending_ = false;
        if (!adv_) return;
        auto r = adv_->set_mode(pending_mode_, false);
        sync_bus_counters();
        if (!r) {
            last_error_ = r.error();
            note_i2c(I2cOutcome::Failed);
            return;
        }
        const std::uint32_t failed_before = breaker_.failures();
        auto v = adv_->verify(pending_mode_, false);
        stats_.i2c_verify_errors += breaker_.failures() - failed_before;
        if (!v) {
            last_error_ = v.error();
            note_i2c(I2cOutcome::Failed);
            return;
        }
        note_i2c(v->matched ? I2cOutcome::Verified : I2cOutcome::Configured);
        return;
    }

    const std::int64_t elapsed_ms = (clock_->now().count() - stage_ns_) / 1'000'000;
    if (elapsed_ms < kModeSettleMs) return;

    mode_pending_ = false;
    note_i2c(I2cOutcome::Noemit);
}

void VideoPump::load_spec_from_ini() {
    ini_read_ = true;
    ++stats_.ini_reads;
    for (auto& s : vfilter_seeds_)
        s.clear();

    spec_pal_.clear();
    spec_ntsc_.clear();
    vsync_adjust_ = 0;
    refresh_min_ = 0.0;
    refresh_max_ = 0.0;
    direct_video_ = false;
    direct_video_ini_ = 0;
    menu_pal_ = false;
    forced_scandoubler_ = false;

    svc::Modeline::WireOptions opt{};
    svc::adv7513::InitOptions io{};
    bool dv = false;
    std::uint8_t sq = 0;
    const auto commit_options = [&] {
        video_->set_wire_options(opt);
        init_opts_ = io;
        spd_direct_video_ = dv;
        spd_quirk_ = sq;
    };

    auto bytes = read_all(*vfs_, "MiSTer.ini");
    if (!bytes) {
        if (vsync_force_) {
            vsync_adjust_ = *vsync_force_;
            opt.vsync_align = false;
        }
        commit_options();
        return;
    }

    svc::ConfigParser::PassNames names{};
    auto snap = svc::ConfigParser::parse_two_pass(
        std::string_view{reinterpret_cast<const char*>(bytes->data()), bytes->size()}, names);
    if (!snap) {
        if (vsync_force_) {
            vsync_adjust_ = *vsync_force_;
            opt.vsync_align = false;
        }
        commit_options();
        return;
    }

    direct_video_ini_ = svc::direct_video_resolved(*snap);

    const bool dv_on = direct_video_ini_ != 0 && !(replay_ini_ && !strict_direct_video_);
    opt.direct_video = dv_on;
    dv = dv_on;
    sq = snap->spd_quirk;

    for (int i = 0; i < 2; ++i) {
        std::uint32_t num = 0, den = 0;
        const char* t = snap->custom_aspect_ratio[i];
        if (t[0] != '\0' && std::sscanf(t, "%u:%u", &num, &den) == 2 && num >= 1 && num <= 4095 &&
            den >= 1 && den <= 4095) {
            ar_cust_[i * 2] = static_cast<std::uint16_t>(num);
            ar_cust_[i * 2 + 1] = static_cast<std::uint16_t>(den);
        } else {
            ar_cust_[i * 2] = 0;
            ar_cust_[i * 2 + 1] = 0;
        }
    }

    vfilter_seeds_[0] = snap->vfilter_default;
    vfilter_seeds_[1] = snap->vfilter_vertical_default;
    vfilter_seeds_[2] = snap->vfilter_scanlines_default;
    vfilter_seeds_[3] = snap->vfilter_interlace_default;

    spec_pal_ = snap->video_mode_pal;
    spec_ntsc_ = snap->video_mode_ntsc;
    menu_pal_ = snap->menu_pal != 0;
    forced_scandoubler_ = snap->forced_scandoubler != 0;
    direct_video_ = dv_on;
    vsync_adjust_ = snap->vsync_adjust;

    if (direct_video_) vsync_adjust_ = 0;
    if (vsync_force_ && !direct_video_) {
        vsync_adjust_ = *vsync_force_;
        opt.vsync_align = false;
    } else {
        opt.vsync_align = (vsync_adjust_ == 2);
    }
    refresh_min_ = snap->refresh_min;
    refresh_max_ = snap->refresh_max;

    io.hdmi_game_mode = snap->hdmi_game_mode != 0;
    io.hdr_mode = snap->hdr != 0;
    io.hlg = snap->hdr == 1;
    io.hdmi_limited = snap->hdmi_limited != 0;
    io.ypbpr = (snap->vga_mode_int == 1) && direct_video_ && direct_video_ini_ == 1;
    io.dvi_mode = snap->dvi_mode == 1;
    io.hdmi_audio_96k = snap->hdmi_audio_96k != 0;
    idle_.set_timeout_minutes(snap->hdmi_off);

    io.has_hdmi_int = false;

    commit_options();

    const std::string_view spec{snap->video_mode};
    if (spec.empty()) return;
    if (override_latched_) return;
    latch_spec(spec);
}

Ex<void> VideoPump::apply_now() {
    if (!ini_read_) load_spec_from_ini();
    if (filters_pending_) {
        filters_pending_ = false;
        load_filters();
    }

    configure_transmitter();

    resolve_families();
    return apply_family(svc::VmodeFamily::Default, 0.0);
}

void VideoPump::resolve_families() {
    fam_pal_ = FamilyMode{};
    fam_ntsc_ = FamilyMode{};
    if (direct_video_) {
        std::size_t idx = menu_pal_ ? 2u : 0u;
        if (forced_scandoubler_) ++idx;
        const svc::PresetMode& p = svc::tvmodes()[idx];
        fam_def_ = FamilyMode{};
        fam_def_.configured = true;
        svc::Modeline& m = fam_def_.stored.mode;
        m.hact = p.vpar[0];
        m.hfp = p.vpar[1];
        m.hs = p.vpar[2];
        m.hbp = p.vpar[3];
        m.vact = p.vpar[4];
        m.vfp = p.vpar[5];
        m.vs = p.vpar[6];
        m.vbp = p.vpar[7];
        m.fpix_mhz = p.fpix_mhz;
        m.vic = p.vic;
        m.pixel_repeat = p.pr != 0;
        families_valid_ = true;
        return;
    }
    const std::string_view def =
        spec_valid_ ? std::string_view{spec_, spec_len_} : std::string_view{};
    fam_def_ = resolve_one(def, true);
    fam_pal_ = resolve_one(spec_pal_, false);
    fam_ntsc_ = resolve_one(spec_ntsc_, false);
    families_valid_ = true;
}

VideoPump::FamilyMode VideoPump::resolve_one(std::string_view spec, bool count_fallback) {
    FamilyMode out{};
    if (!spec.empty()) {
        if (auto parsed = video_->parse_mode(spec)) {
            out.stored =
                svc::VideoService::store_mode(*parsed, support_fhd_, video_->supports_pr());
            if (parsed->how == svc::ModeRequest::PresetIndex) {
                out.index = static_cast<std::uint8_t>(parsed->preset_index & 0xFFu);
            }
            if (parsed->raw_pll) {
                out.have_raw_pll = true;
                out.raw_pll = *parsed->raw_pll;
            }
            out.configured = out.stored.explicitly_requested || out.stored.fully_custom;
            return out;
        }
    }
    if (count_fallback) ++stats_.parse_fallbacks;
    out.stored = svc::VideoService::preset_fallback(support_fhd_, video_->supports_pr());
    out.index = support_fhd_ ? 8u : 0u;
    return out;
}

const VideoPump::FamilyMode& VideoPump::family(svc::VmodeFamily f) const noexcept {
    if (f == svc::VmodeFamily::Pal) return fam_pal_;
    if (f == svc::VmodeFamily::Ntsc) return fam_ntsc_;
    return fam_def_;
}

void VideoPump::service_family_switch() {
    if (!families_valid_ || front_end_) return;
    if (vsync_adjust_ == 0) return;
    const std::uint32_t vtime = video_->info().vtime;
    const auto choice = svc::VideoService::select_family(vtime, vsync_adjust_ != 0,
                                                         fam_pal_.configured, fam_ntsc_.configured);
    double fpix_mhz = 0.0;
    if (choice.adjustable) {
        if (const auto est = svc::VideoService::estimate_fpix(family(choice.family).stored.mode,
                                                              vtime, refresh_min_, refresh_max_)) {
            fpix_mhz = *est;
        }
    }
    ++stats_.family_switches;
    if (const auto r = apply_family(choice.family, fpix_mhz); !r) {
        last_error_ = r.error();
        return;
    }

    video_->request_force_sample();
}

Ex<void> VideoPump::apply_family(svc::VmodeFamily f, double fpix_mhz) {
    const FamilyMode& fm = family(f);
    const bool adjusted = fpix_mhz > 0.0;
    const double clock = adjusted ? fpix_mhz : fm.stored.mode.fpix_mhz;

    const std::uint32_t watermark = wire_.stats().emitted;

    stats_.last_mode_index = fm.index;
    stats_.last_fpix_khz = fpix_khz(clock);

    if (fm.have_raw_pll && !adjusted) {
        video_->set_pll_block(fm.raw_pll);
    } else {
        const auto p = svc::PllSolver::solve(clock);
        if (!p) {
            ++stats_.solve_failures;
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), fpix_detail(clock)});
        }
        if (!pll_usable(*p)) {
            ++stats_.zero_divider;
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), fpix_detail(clock)});
        }
        if (p->approximated) ++stats_.approximated;

        if (!adjusted) video_->set_pll_block(svc::PllSolver::block(*p));
    }

    svc::Modeline m = fm.stored.mode;
    m.fpix_mhz = fpix_mhz;
    const auto r = video_->apply(m);
    if (!r) {
        ++stats_.stage_failures;
        return r;
    }
    ++stats_.applies;
    output_locked_ = vsync_adjust_ == 2 && adjusted;
    last_apply_ns_ = clock_->now().count();

    if (adv_ && init_ok_) {
        pending_mode_ = fm.stored.mode;
        emit_watermark_ = watermark;
        stage_ns_ = last_apply_ns_;
        mode_pending_ = true;
    } else {
        mode_pending_ = false;
    }
    scrw_ = static_cast<std::uint16_t>(fm.stored.mode.hact);
    scrh_ = static_cast<std::uint16_t>(fm.stored.mode.vact);
    publish_scaling_policy();
    return {};
}

}  // namespace mister::app
