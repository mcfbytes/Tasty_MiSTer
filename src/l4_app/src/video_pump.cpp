// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/video_pump.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <cerrno>
#include <ctime>
#include <utility>
#include <vector>

#include "infra/byte_view.h"
#include "svc/config.h"
#include "svc/config_parser.h"
#include "svc/filter_store.h"
#include "svc/vfs.h"

namespace mister::app {

namespace {

Ex<std::vector<std::byte>> read_all(const svc::Vfs& vfs, std::string_view path) {
    auto f = vfs.open(path, svc::OpenMode::ReadWhole);
    if (!f)
        return std::unexpected(f.error());
    auto sz = (*f)->size();
    if (!sz)
        return std::unexpected(sz.error());
    std::vector<std::byte> buf(static_cast<std::size_t>(sz->v));
    std::size_t off = 0;
    while (off < buf.size()) {
        auto n = (*f)->read_at(off, std::span<std::byte>(buf).subspan(off));
        if (!n)
            return std::unexpected(n.error());
        if (*n == 0) {
            return std::unexpected(
                Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(off)}
            );
        }
        off += *n;
    }
    return buf;
}

std::uint32_t fpix_detail(double f) noexcept {
    if (!(f > 0.0) || !(f < 4.0e9))
        return 0u;
    return static_cast<std::uint32_t>(f);
}

std::uint32_t fpix_khz(double mhz) noexcept {
    if (!(mhz > 0.0) || !(mhz < 4.0e6))
        return 0u;
    return static_cast<std::uint32_t>(mhz * 1000.0 + 0.5);
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

    video_->set_hdmi_capable(false);
    publish_scaling_policy();
}

Adv7513Seat::Host VideoPump::seat_host() noexcept {
    return Adv7513Seat::Host{*video_, wire_, stats_, last_error_};
}

void VideoPump::take_edge(Adv7513Seat::Edge e) noexcept {
    if (e == Adv7513Seat::Edge::Lost)
        mode_pending_ = false;
    if (e == Adv7513Seat::Edge::Owed)
        pending_ = true;
}

void VideoPump::load_filters() {

    auto set = std::make_unique<svc::FilterSet>();
    const svc::ScalerSeeds seeds{
        ini_.vfilter_seeds[0],
        ini_.vfilter_seeds[1],
        ini_.vfilter_seeds[2],
        ini_.vfilter_seeds[3]
    };
    svc::load_filter_set(vfs_, std::string_view{core_name_, core_name_len_}, seeds, *set);
    wire_.publish_filters(*set);
    filter_mode_ = set->modes[0];
    const bool enabled[4] =
        {set->modes[0] != 0, set->modes[1] != 0, set->modes[2] != 0, set->modes[3] != 0};
    video_->set_filter_slots(std::span<const bool>(enabled, 4));
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
    if (v > 1)
        v = 0;
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
    filter_mode_ = 0;

    families_valid_ = false;

    seat_.on_core_loaded(seat_host());
    front_end_ = front_end;
    video_->reset_resolution_gate();
    geo_edges_ = 0;
    geo_gen_seen_ = video_->resolution_generation().v;
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
    const auto g = video_->resolution_generation().v;
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
    seat_.report(stats_);
    stats_cell_.publish(stats_);

    GeometryRecord g{};
    const svc::VideoSample& s = video_->info();
    g.valid = geo_edges_ != 0u || s.res != 0u || s.width != 0u;
    g.fb_en = s.fb_en();
    g.rotated = s.rotated();
    g.res = s.res;
    g.width = s.width;
    g.height = s.height;
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
    wire_.publish_ar_custom(ini_.ar_cust[0], ini_.ar_cust[1], ini_.ar_cust[2], ini_.ar_cust[3]);
    wire_.publish_shadow_mask(0x0000);
}

void VideoPump::service_idle_blank() {

    const auto want = idle_.tick(
        clock_.now().count(),
        activity_.activity_seq(),
        activity_.input_grabbed()
    );
    if (!want.has_value())
        return;
    if (auto r = video_->request_power(*want); !r)
        last_error_ = r.error();
}

void VideoPump::service_recovery() {
    if (i2c_held_)
        return;
    take_edge(seat_.recover(seat_host()));
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

    const std::int64_t elapsed_ms = (clock_.now().count() - stage_ns_) / 1'000'000;
    if (elapsed_ms < kModeSettleMs)
        return;

    mode_pending_ = false;
    note_i2c(I2cOutcome::Noemit);
}

void VideoPump::load_spec_from_ini() {
    ini_read_ = true;
    ++stats_.ini_reads;

    auto bytes = read_all(vfs_, "MiSTer.ini");
    if (!bytes) {
        ini_ = absent_video_ini(ini_force_, ini_);
        return;
    }

    svc::config_parser::PassNames names{};
    const auto snap = svc::config_parser::parse_two_pass(infra::as_text(std::span(*bytes)), names);
    ini_ = parse_video_ini(snap, ini_force_);
    idle_.set_timeout_minutes(snap.hdmi_off);

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
        Adv7513Seat::Spd{
            std::string_view{core_name_, core_name_len_},
            ini_.direct_video,
            ini_.spd_quirk
        }
    ));

    resolve_families();
    return apply_family(svc::VmodeFamily::Default, 0.0);
}

void VideoPump::resolve_families() {
    family(svc::VmodeFamily::Pal) = FamilyMode{};
    family(svc::VmodeFamily::Ntsc) = FamilyMode{};
    if (ini_.direct_video) {
        std::size_t idx = ini_.menu_pal ? 2u : 0u;
        if (ini_.forced_scandoubler)
            ++idx;
        const svc::PresetMode& p = svc::tvmodes()[idx];
        FamilyMode& def = family(svc::VmodeFamily::Default);
        def = FamilyMode{};
        def.configured = true;
        svc::Modeline& m = def.stored.mode;
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
    const std::string_view def = spec_valid_ ? std::string_view{spec_, spec_len_}
                                             : std::string_view{};
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
                support_fhd_,
                video_->supports_pr()
            );
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
    if (count_fallback)
        ++stats_.parse_fallbacks;
    out.stored = svc::video_timing::preset_fallback(support_fhd_, video_->supports_pr());
    out.index = support_fhd_ ? 8u : 0u;
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
    if (ini_.vsync_adjust == 0)
        return;
    const std::uint32_t vtime = video_->info().vtime;
    const auto choice = svc::video_timing::select_family(
        vtime,
        ini_.vsync_adjust != 0,
        family(svc::VmodeFamily::Pal).configured,
        family(svc::VmodeFamily::Ntsc).configured
    );
    double fpix_mhz = 0.0;
    if (choice.adjustable) {
        if (const auto est = svc::video_timing::estimate_fpix(
                family(choice.family).stored.mode,
                vtime,
                ini_.refresh_min,
                ini_.refresh_max
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
    output_locked_ = ini_.vsync_adjust == 2 && adjusted;
    last_apply_ns_ = clock_.now().count();

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
