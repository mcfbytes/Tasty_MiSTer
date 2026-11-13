// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/video_timing.h"

#include <array>
#include <cmath>

namespace mister::svc {

namespace {

constexpr double kFpixMinMhz = static_cast<double>(2.f);
constexpr double kFpixMaxMhz = static_cast<double>(300.f);
static_assert(kFpixMinMhz == 2.0 && kFpixMaxMhz == 300.0);

constexpr auto kVmodes = std::to_array<PresetMode>({
    {.h = {1280, 110, 40, 220}, .v = {720, 5, 5, 20}, .fpix_mhz = 74.25, .vic = 4, .pr = 0},
    {.h = {1024, 24, 136, 160}, .v = {768, 3, 6, 29}, .fpix_mhz = 65.0, .vic = 0, .pr = 0},
    {.h = {720, 16, 62, 60}, .v = {480, 9, 6, 30}, .fpix_mhz = 27.0, .vic = 3, .pr = 0},
    {.h = {720, 12, 64, 68}, .v = {576, 5, 5, 39}, .fpix_mhz = 27.0, .vic = 18, .pr = 0},
    {.h = {1280, 48, 112, 248}, .v = {1024, 1, 3, 38}, .fpix_mhz = 108.0, .vic = 0, .pr = 0},
    {.h = {800, 40, 128, 88}, .v = {600, 1, 4, 23}, .fpix_mhz = 40.0, .vic = 0, .pr = 0},
    {.h = {640, 16, 96, 48}, .v = {480, 10, 2, 33}, .fpix_mhz = 25.175, .vic = 1, .pr = 0},
    {.h = {1280, 440, 40, 220}, .v = {720, 5, 5, 20}, .fpix_mhz = 74.25, .vic = 19, .pr = 0},
    {.h = {1920, 88, 44, 148}, .v = {1080, 4, 5, 36}, .fpix_mhz = 148.5, .vic = 16, .pr = 0},
    {.h = {1920, 528, 44, 148}, .v = {1080, 4, 5, 36}, .fpix_mhz = 148.5, .vic = 31, .pr = 0},
    {.h = {1366, 70, 143, 213}, .v = {768, 3, 3, 24}, .fpix_mhz = 85.5, .vic = 0, .pr = 0},
    {.h = {1024, 40, 104, 144}, .v = {600, 1, 3, 18}, .fpix_mhz = 48.96, .vic = 0, .pr = 0},
    {.h = {1920, 48, 32, 80}, .v = {1440, 2, 4, 38}, .fpix_mhz = 185.203, .vic = 0, .pr = 0},
    {.h = {2048, 48, 32, 80}, .v = {1536, 2, 4, 38}, .fpix_mhz = 209.318, .vic = 0, .pr = 0},

    {.h = {1280, 24, 16, 40}, .v = {1440, 3, 5, 33}, .fpix_mhz = 120.75, .vic = 0, .pr = 1},
});
static_assert(kVmodes.size() == kNumVmodes, "vmodes: one row per preset index");

constexpr auto kTvmodes = std::to_array<PresetMode>({
    {.h = {640, 30, 60, 70}, .v = {240, 4, 4, 14}, .fpix_mhz = 12.587, .vic = 0, .pr = 0},
    {.h = {640, 16, 96, 48}, .v = {480, 8, 4, 33}, .fpix_mhz = 25.175, .vic = 0, .pr = 0},
    {.h = {640, 30, 60, 70}, .v = {288, 6, 4, 14}, .fpix_mhz = 12.587, .vic = 0, .pr = 0},
    {.h = {640, 16, 96, 48}, .v = {576, 2, 4, 42}, .fpix_mhz = 25.175, .vic = 0, .pr = 0},
});
static_assert(kTvmodes.size() == kNumTvmodes, "tvmodes: one row per TV mode");

}

std::span<const PresetMode> vmodes() noexcept {
    return std::span<const PresetMode>(kVmodes);
}

std::span<const PresetMode> tvmodes() noexcept {
    return std::span<const PresetMode>(kTvmodes);
}

namespace video_timing {

Modeline with_timings(Modeline m, std::span<const std::uint32_t, kModelineTimings> t) noexcept {
    m.h = AxisTiming{t[0], t[1], t[2], t[3]};
    m.v = AxisTiming{t[4], t[5], t[6], t[7]};
    return m;
}

Modeline preset_modeline(const PresetMode& p) noexcept {
    Modeline m{};
    m.h = p.h;
    m.v = p.v;
    m.vic = p.vic;
    m.pixel_repeat = (p.pr != 0);
    m.fpix_mhz = p.fpix_mhz;
    return m;
}

namespace {

constexpr double kFabricClockMhz = 100.0;
constexpr double kFabricClockHz = 100'000'000.0;
constexpr float kFabricClockHzF = 100'000'000.0f;
constexpr double kHzPerMhz = 1'000'000.0;

constexpr PresetIndex kPresetDefault{0};
constexpr PresetIndex kPreset1080p60{8};

StoredMode apply_preset(PresetIndex preset, bool pixel_repeat) {
    const std::span<const PresetMode> table = vmodes();

    if (preset.in(table).pr == 1 && !pixel_repeat)
        preset = kPreset1080p60;

    const PresetMode& p = preset.in(table);
    StoredMode out{};
    out.mode = preset_modeline(p);
    out.mode.rb = 1;
    return out;
}

}

StoredMode store_mode(const ParsedMode& parsed, CoreVideoCaps caps) {
    StoredMode out{};
    if (const PresetPick* pick = std::get_if<PresetPick>(&parsed)) {
        out = apply_preset(pick->index, caps.pixel_repeat);

        out.mode.hpol = pick->hpol;
        out.mode.vpol = pick->vpol;
        out.explicitly_requested = true;
        return out;
    }

    out.mode = std::get<CustomMode>(parsed).mode;
    out.fully_custom = true;
    out.explicitly_requested = false;
    return out;
}

StoredMode preset_fallback(CoreVideoCaps caps) {
    StoredMode out = apply_preset(
        caps.full_hd ? kPreset1080p60 : kPresetDefault,
        caps.pixel_repeat
    );
    out.explicitly_requested = false;
    return out;
}

VFilter select_vfilter(const VideoSample& s, ScalerSlots<bool> slot_enabled) {

    const auto has = [&](VFilter f) {
        return slot_enabled[ScalerSlotIndex::of(f)];
    };
    if (s.interlaced()) {
        return has(VFilter::Ilace) ? VFilter::Ilace : VFilter::Horz;
    }
    if (s.flt_flags.core_scanlines() && has(VFilter::Scanlines))
        return VFilter::Scanlines;
    if (has(VFilter::Vert))
        return VFilter::Vert;
    return VFilter::Horz;
}

bool RefreshBand::contains(float hz) const noexcept {
    if (min_hz != 0.0 && hz < static_cast<float>(min_hz))
        return false;
    if (max_hz != 0.0 && hz > static_cast<float>(max_hz))
        return false;
    return true;
}

std::optional<double> estimate_fpix(const Modeline& m, std::uint32_t vtime, RefreshBand band) {

    if (vtime == 0)
        return std::nullopt;
    const double htotal = static_cast<double>(m.h.total());
    const double vtotal = static_cast<double>(m.v.total());
    double fpix = kFabricClockMhz * htotal * vtotal;
    fpix /= static_cast<double>(vtime);

    if (fpix < kFpixMinMhz || fpix > kFpixMaxMhz)
        return std::nullopt;

    const float hz = kFabricClockHzF / static_cast<float>(vtime);
    if (!band.contains(hz))
        return std::nullopt;
    return fpix;
}

std::optional<ScanrateLock> lock_scanrate(const Modeline& m, std::uint32_t vtime) noexcept {
    if (vtime == 0)
        return std::nullopt;
    const std::int64_t htotal = static_cast<std::int64_t>(m.h.total());
    if (htotal <= 0 || !(m.fpix_mhz > 0.0))
        return std::nullopt;

    const std::int64_t vbp = static_cast<std::int64_t>(m.v.back_porch);
    const std::int64_t min_vbp = (vbp < kScanlockMinVbp) ? vbp : kScanlockMinVbp;
    const std::int64_t vt_min = static_cast<std::int64_t>(m.v.active) + m.v.sync + min_vbp + 1;

    const double vtotal_real = m.fpix_mhz
        * static_cast<double>(vtime)
        / (static_cast<double>(htotal) * kFabricClockMhz);

    if (!(vtotal_real >= 0.0 && vtotal_real < kScanlockVtotalRoundCeiling))
        return std::nullopt;
    const std::int64_t vtotal = std::llround(vtotal_real);
    if (vt_min > kScanlockVtotalMax || vtotal < vt_min)
        return std::nullopt;

    const std::int64_t vblank = vtotal - static_cast<std::int64_t>(m.v.active) - m.v.sync;
    const std::int64_t vfp = (vblank - vbp < 1) ? 1 : vblank - vbp;

    ScanrateLock out{};
    out.vfp = static_cast<std::uint32_t>(vfp);
    out.vbp = static_cast<std::uint32_t>(vblank - vfp);
    out.vtotal = static_cast<std::uint32_t>(vtotal);
    out.target_scanrate_hz = (m.fpix_mhz * kHzPerMhz) / static_cast<double>(htotal);
    out.actual_scanrate_hz = static_cast<double>(vtotal)
        * (kFabricClockHz / static_cast<double>(vtime));
    return out;
}

bool rotation_needs_buffered_scaler(
    const Modeline& m,
    const std::optional<ScanrateLock>& lock,
    const VideoSample& s
) noexcept {
    if (!s.rotated() || s.htime == 0 || s.vtime == 0)
        return false;
    const std::uint32_t vfp = lock ? lock->vfp : m.v.front_porch;
    const std::uint32_t vbp = lock ? lock->vbp : m.v.back_porch;
    const std::int64_t vtotal = static_cast<std::int64_t>(m.v.active) + vfp + m.v.sync + vbp;
    if (vtotal <= 0)
        return false;
    const std::int64_t vblank = vtotal - static_cast<std::int64_t>(m.v.active);
    return (vblank * static_cast<std::int64_t>(s.vtime) / vtotal)
        > (static_cast<std::int64_t>(s.de_v) + kScanlockRotMargin)
        * static_cast<std::int64_t>(s.htime);
}

FamilyChoice select_family(std::uint32_t vtime, FamilyConfig cfg) noexcept {

    FamilyChoice out{};
    if (vtime == 0 || !cfg.switching)
        return out;
    if (cfg.have_pal || cfg.have_ntsc) {
        if (vtime > kPalVtimeTicks) {
            out.family = cfg.have_pal ? VmodeFamily::Pal : VmodeFamily::Ntsc;
            out.adjustable = cfg.have_pal;
        } else {
            out.family = cfg.have_ntsc ? VmodeFamily::Ntsc : VmodeFamily::Pal;
            out.adjustable = cfg.have_ntsc;
        }
        return out;
    }
    out.adjustable = true;
    return out;
}

}

}
