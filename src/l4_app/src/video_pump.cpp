// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/video_pump.h"

#include "app/input_wire.h"

#include <cstdio>
#include <chrono>
#include <memory>
#include <cerrno>
#include <ctime>
#include <utility>

#include "app/ini_parse.h"
#include "infra/byte_view.h"
#include "svc/config.h"
#include "svc/config_parser.h"
#include "svc/filter_store.h"
#include "svc/pll_solver.h"
#include "svc/vfs.h"

namespace mister::app {

namespace {

std::uint32_t fpix_detail(double f) noexcept {
    constexpr double kFpixHzCeiling = 4.0e9;
    if (!(f > 0.0) || !(f < kFpixHzCeiling))
        return 0u;
    return static_cast<std::uint32_t>(f);
}

std::uint32_t fpix_khz(double mhz) noexcept {
    constexpr double kFpixMhzCeiling = 4.0e6;
    constexpr double kKhzPerMhz = 1000.0;
    constexpr double kRoundHalf = 0.5;
    if (!(mhz > 0.0) || !(mhz < kFpixMhzCeiling))
        return 0u;
    return static_cast<std::uint32_t>(mhz * kKhzPerMhz + kRoundHalf);
}

}

VideoPump::VideoPump(
    const svc::Vfs& vfs,
    const os::IClock& clock,
    const hal::VideoOutDecl& board,
    infra::OptRef<svc::adv7513::II2cAdapter> i2c,
    const hal::IHdmiInterrupt& hdmi_int,
    Wiring wiring
) noexcept
    : vfs_(vfs),
      clock_(clock),
      seat_(clock, board, i2c, hdmi_int),
      wire_(VideoWire::Wiring{.link_tx = wiring.link_tx}),
      video_(
          std::in_place,
          svc::VideoService::Wiring{
              infra::OptRef<svc::VideoService::IVideoWireSink>{wire_},
              infra::OptRef<svc::VideoService::IResolutionSampler>{wire_}
          },
          seat_.io()
      ),
      activity_(wiring.activity) {

    video_->hdmi().set_hdmi_capable(false);
}

Adv7513Seat::Host VideoPump::seat_host() noexcept {
    return Adv7513Seat::Host{video_->hdmi(), wire_, stats_, last_error_};
}

void VideoPump::take_edge(Adv7513Seat::Edge e) noexcept {
    if (e == Adv7513Seat::Edge::Lost)
        mode_pending_ = false;
    if (e == Adv7513Seat::Edge::Owed)
        pending_ = true;
}

void VideoPump::load_filters() {

    auto set = std::make_unique<svc::FilterSet>();
    svc::ScalerSeeds seeds{};
    seeds[svc::ScalerSlotIndex::of(svc::VFilter::Horz)] = ini_.vfilter_seeds.horz;
    seeds[svc::ScalerSlotIndex::of(svc::VFilter::Vert)] = ini_.vfilter_seeds.vert;
    seeds[svc::ScalerSlotIndex::of(svc::VFilter::Scanlines)] = ini_.vfilter_seeds.scan;
    seeds[svc::ScalerSlotIndex::of(svc::VFilter::Ilace)] = ini_.vfilter_seeds.ilace;
    svc::load_filter_set(vfs_, core_name_.view(), seeds, *set);
    wire_.publish_filters(*set);
    filter_mode_ = set->filter_mode();
    ++stats_.filter_loads;
    publish_scaling_policy();
}

void VideoPump::latch_spec(std::string_view spec) noexcept {
    (void)spec_.assign(spec);
}

void VideoPump::force_vsync_adjust(VsyncAdjust v) noexcept {
    if (v == VsyncAdjust::LowLag)
        v = VsyncAdjust::Off;
    ini_force_.vsync = v;
    ini_.vsync_adjust = v;
}

void VideoPump::arm_replay_ini(bool strict) noexcept {
    ini_force_.replay = true;
    ini_force_.strict_direct_video = strict;
}

void VideoPump::set_mode_override(std::string_view spec) noexcept {
    latch_spec(spec);
    override_latched_ = true;
}

bool VideoPump::take_fb_cmd(std::string_view) noexcept {
    TASTY_SEAT_BODY(VideoPump);
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
    ini_read_ = false;

    filters_pending_ = true;
    filter_mode_ = svc::ScalerSlotMode::off();

    families_valid_ = false;

    seat_.on_core_loaded(seat_host());
    front_end_ = front_end;
    video_->reset_resolution_gate();
    geo_edges_ = 0;
    geo_gen_seen_ = video_->resolution_generation();
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

    if (i2c_held_)
        return;
    service_recovery();
    service_mode_phase();

    ++geo_polls_;
    if (const auto r = video_->poll_resolution(); !r) {
        if (r.error().code != Errc::not_found)
            last_error_ = r.error();
    }
    const auto g = video_->resolution_generation();
    if (g != geo_gen_seen_) {
        geo_gen_seen_ = g;
        ++geo_edges_;
        service_family_switch();
    }
    take_edge(seat_.hotplug(seat_host(), ini_.init));
    service_idle_blank();

    if (!pending_)
        return;
    pending_ = false;
    if (const auto r = apply_now(); !r)
        last_error_ = r.error();
}

void VideoPump::publish_monitoring() noexcept {
    Adv7513Seat::apply(seat_.report(), stats_);
    stats_cell_.publish(stats_);

    GeometryRecord g{};
    const svc::VideoSample& s = video_->info();
    g.valid = geo_edges_ != 0u || s.res.v != 0u || s.active.width != 0u;
    g.fb_en = s.fb_en();
    g.rotated = s.rotated();
    g.res = s.res.v;
    g.width = s.active.width;
    g.height = s.active.height;
    g.vtime = s.vtime;
    g.core_seq = wire_.sampled_core_seq();
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
    wire_.publish_ar_custom(ar_custom_words(ini_.ar_cust));
    wire_.publish_shadow_mask(0x0000);
}

void VideoPump::service_idle_blank() {

    const auto want = idle_.tick(clock_.now(), activity_.activity_seq(), activity_.input_grabbed());
    if (!want.has_value())
        return;

    const auto bus = seat_.bus();
    if (!bus)
        return;
    if (auto r = video_->hdmi().request_power(*bus, *want); !r)
        last_error_ = r.error();
}

void VideoPump::service_recovery() {
    if (i2c_held_)
        return;
    take_edge(seat_.recover(seat_host()));
}

void VideoPump::set_core_name(std::string_view name) noexcept {
    (void)core_name_.assign(name);
}

void VideoPump::note_i2c(I2cOutcome o) noexcept {
    if (!seat_.init_ok() && stats_.i2c == I2cOutcome::Failed && o != I2cOutcome::Failed) {
        return;
    }
    stats_.i2c = o;
}

void VideoPump::service_mode_phase() {
    if (!mode_pending_)
        return;

    const std::uint32_t emitted = wire_.stats().emitted;
    if (emitted > emit_watermark_) {
        mode_pending_ = false;
        if (const auto o = seat_.program_mode(seat_host(), pending_mode_))
            note_i2c(*o);
        return;
    }

    constexpr std::int64_t kNsPerMs = 1'000'000;
    const std::int64_t elapsed_ms = (clock_.now().time_since_epoch().count() - stage_ns_)
        / kNsPerMs;
    if (elapsed_ms < kModeSettleMs)
        return;

    mode_pending_ = false;
    note_i2c(I2cOutcome::Noemit);
}

void VideoPump::load_spec_from_ini() {
    ini_read_ = true;
    ++stats_.ini_reads;

    auto bytes = read_ini_whole(vfs_);
    if (!bytes) {
        ini_ = absent_video_ini(ini_force_, ini_);
        return;
    }

    svc::config_parser::PassNames names{};
    const auto snap = svc::config_parser::parse_two_pass(infra::as_text(std::span(*bytes)), names);
    ini_ = parse_video_ini(snap, ini_force_);
    idle_.set_timeout_minutes(std::chrono::minutes{snap.hdmi_off});

    const std::string_view spec{snap.video_mode};
    if (spec.empty())
        return;
    if (override_latched_)
        return;
    latch_spec(spec);
}

Ex<void> VideoPump::apply_now() {
    if (!ini_read_)
        load_spec_from_ini();
    if (filters_pending_) {
        filters_pending_ = false;
        load_filters();
    }

    take_edge(seat_.configure(
        seat_host(),
        ini_.init,
        Adv7513Seat::Spd{core_name_.view(), ini_.direct_video, ini_.spd_quirk}
    ));

    resolve_families();
    return apply_family(svc::VmodeFamily::Default, 0.0);
}

void VideoPump::resolve_families() {
    family(svc::VmodeFamily::Pal) = FamilyMode{};
    family(svc::VmodeFamily::Ntsc) = FamilyMode{};
    if (ini_.direct_video) {
        constexpr std::size_t kPresetDefault = 0;
        constexpr std::size_t kPresetPal240 = 2;
        std::size_t idx = ini_.menu_pal ? kPresetPal240 : kPresetDefault;
        if (ini_.forced_scandoubler)
            ++idx;
        const svc::PresetMode& p = svc::tvmodes()[idx];
        FamilyMode& def = family(svc::VmodeFamily::Default);
        def = FamilyMode{};
        def.configured = true;
        def.stored.mode = svc::video_timing::preset_modeline(p);
        families_valid_ = true;
        return;
    }
    const std::string_view def = spec_.view();
    family(svc::VmodeFamily::Default) = resolve_one(def, true);
    family(svc::VmodeFamily::Pal) = resolve_one(ini_.spec_pal, false);
    family(svc::VmodeFamily::Ntsc) = resolve_one(ini_.spec_ntsc, false);
    families_valid_ = true;
}

VideoPump::FamilyMode VideoPump::resolve_one(std::string_view spec, bool count_fallback) {
    FamilyMode out{};
    if (!spec.empty()) {
        if (auto parsed = video_->parse_mode(spec)) {
            out.stored = svc::video_timing::store_mode(
                *parsed,
                {.full_hd = support_fhd_, .pixel_repeat = video_->supports_pr()}
            );
            if (parsed->how == svc::ModeRequest::PresetIndex) {
                out.index = VideoModeIndex::from_preset(parsed->preset_index);
            }
            if (parsed->raw_pll) {
                out.have_raw_pll = true;
                out.raw_pll = *parsed->raw_pll;
            }
            out.configured = out.stored.explicitly_requested || out.stored.fully_custom;
            return out;
        }
    }
    if (count_fallback)
        ++stats_.parse_fallbacks;
    out.stored = svc::video_timing::preset_fallback(
        {.full_hd = support_fhd_, .pixel_repeat = video_->supports_pr()}
    );
    constexpr VideoModeIndex kPresetDefault{0};
    constexpr VideoModeIndex kPreset1080p60{8};
    out.index = support_fhd_ ? kPreset1080p60 : kPresetDefault;
    return out;
}

static_assert(
    static_cast<std::size_t>(svc::VmodeFamily::Ntsc) + 1u == 3u,
    "families_ holds one FamilyMode per VmodeFamily"
);

VideoPump::FamilyMode& VideoPump::family(svc::VmodeFamily f) noexcept {
    return families_[static_cast<std::size_t>(f)];
}

void VideoPump::service_family_switch() {
    if (!families_valid_ || front_end_)
        return;
    if (ini_.vsync_adjust == VsyncAdjust::Off)
        return;
    const std::uint32_t vtime = video_->info().vtime;
    const auto choice = svc::video_timing::select_family(
        vtime,
        {.switching = ini_.vsync_adjust != VsyncAdjust::Off,
         .have_pal = family(svc::VmodeFamily::Pal).configured,
         .have_ntsc = family(svc::VmodeFamily::Ntsc).configured}
    );
    double fpix_mhz = 0.0;
    if (choice.adjustable) {
        if (const auto est = svc::video_timing::estimate_fpix(
                family(choice.family).stored.mode,
                vtime,
                {.min_hz = ini_.refresh_min, .max_hz = ini_.refresh_max}
            )) {
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

    svc::PllBlock pll_block{};
    if (fm.have_raw_pll && !adjusted) {
        pll_block = fm.raw_pll;
    } else {
        const auto p = svc::pll::solve(clock);
        if (!p) {
            ++stats_.solve_failures;
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), fpix_detail(clock)});
        }
        if (!pll_usable(*p)) {
            ++stats_.zero_divider;
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), fpix_detail(clock)});
        }
        if (p->approximated)
            ++stats_.approximated;

        if (!adjusted)
            pll_block = svc::pll::block(*p);
    }

    svc::Modeline m = fm.stored.mode;
    m.fpix_mhz = fpix_mhz;
    const auto r = video_->apply(m, pll_block, ini_.wire);
    if (!r) {
        ++stats_.stage_failures;
        return r;
    }
    ++stats_.applies;
    output_locked_ = ini_.vsync_adjust == VsyncAdjust::LowLag && adjusted;
    last_apply_ns_ = clock_.now().time_since_epoch().count();

    if (seat_.attached() && seat_.init_ok()) {
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

}
