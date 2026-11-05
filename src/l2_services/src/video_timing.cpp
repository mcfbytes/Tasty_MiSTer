// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/video_timing.h"

#include <cmath>

#include "video_service_detail.h"

namespace mister::svc {

namespace {

constexpr PresetMode kVmodes[kNumVmodes] = {
    {{1280, 110, 40, 220, 720, 5, 5, 20}, 74.25, 4, 0},
    {{1024, 24, 136, 160, 768, 3, 6, 29}, 65.0, 0, 0},
    {{720, 16, 62, 60, 480, 9, 6, 30}, 27.0, 3, 0},
    {{720, 12, 64, 68, 576, 5, 5, 39}, 27.0, 18, 0},
    {{1280, 48, 112, 248, 1024, 1, 3, 38}, 108.0, 0, 0},
    {{800, 40, 128, 88, 600, 1, 4, 23}, 40.0, 0, 0},
    {{640, 16, 96, 48, 480, 10, 2, 33}, 25.175, 1, 0},
    {{1280, 440, 40, 220, 720, 5, 5, 20}, 74.25, 19, 0},
    {{1920, 88, 44, 148, 1080, 4, 5, 36}, 148.5, 16, 0},
    {{1920, 528, 44, 148, 1080, 4, 5, 36}, 148.5, 31, 0},
    {{1366, 70, 143, 213, 768, 3, 3, 24}, 85.5, 0, 0},
    {{1024, 40, 104, 144, 600, 1, 3, 18}, 48.96, 0, 0},
    {{1920, 48, 32, 80, 1440, 2, 4, 38}, 185.203, 0, 0},
    {{2048, 48, 32, 80, 1536, 2, 4, 38}, 209.318, 0, 0},

    {{1280, 24, 16, 40, 1440, 3, 5, 33}, 120.75, 0, 1},
};

}

std::span<const PresetMode> vmodes() noexcept {
    return std::span<const PresetMode>(kVmodes, kNumVmodes);
}

namespace video_timing {

void assign_timings(Modeline& m, std::span<const std::uint32_t, 8> t) noexcept {
    m.hact = t[0];
    m.hfp = t[1];
    m.hs = t[2];
    m.hbp = t[3];
    m.vact = t[4];
    m.vfp = t[5];
    m.vs = t[6];
    m.vbp = t[7];
}

Modeline preset_modeline(const PresetMode& p) noexcept {
    Modeline m{};
    assign_timings(m, p.vpar);
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

StoredMode apply_preset(std::uint32_t mode, bool supports_pr) {
    constexpr std::uint32_t kPresetDefault = 0;
    constexpr std::uint32_t kPreset1080p60 = 8;
    if (mode >= kNumVmodes)
        mode = kPresetDefault;

    if (kVmodes[mode].pr == 1 && !supports_pr)
        mode = kPreset1080p60;

    const PresetMode& p = kVmodes[mode];
    StoredMode out{};
    out.mode = preset_modeline(p);
    out.mode.rb = 1;
    return out;
}

}

StoredMode store_mode(const ParsedMode& parsed, bool support_fhd, bool supports_pr) {
    if (parsed.how != ModeRequest::PresetIndex) {

        StoredMode out{};
        out.mode = parsed.mode;
        out.fully_custom = true;
        out.explicitly_requested = false;
        return out;
    }
    (void)support_fhd;
    StoredMode out = apply_preset(parsed.preset_index, supports_pr);

    out.mode.hpol = parsed.mode.hpol;
    out.mode.vpol = parsed.mode.vpol;
    out.explicitly_requested = true;
    return out;
}

StoredMode preset_fallback(bool support_fhd, bool supports_pr) {
    constexpr std::uint32_t kPresetDefault = 0;
    constexpr std::uint32_t kPreset1080p60 = 8;
    StoredMode out = apply_preset(support_fhd ? kPreset1080p60 : kPresetDefault, supports_pr);
    out.explicitly_requested = false;
    return out;
}

VFilter select_vfilter(const VideoSample& s, std::span<const bool> slot_enabled) {

    const auto has = [&](VFilter f) {
        const auto i = static_cast<std::size_t>(f);
        return i < slot_enabled.size() && slot_enabled[i];
    };
    constexpr std::uint32_t kFltScanlinesMask = 0x30;
    if (s.interlaced()) {
        return has(VFilter::Ilace) ? VFilter::Ilace : VFilter::Horz;
    }
    if ((s.flt_flags & kFltScanlinesMask) && has(VFilter::Scanlines))
        return VFilter::Scanlines;
    if (has(VFilter::Vert))
        return VFilter::Vert;
    return VFilter::Horz;
}

std::optional<double> estimate_fpix(
    const Modeline& m,
    std::uint32_t vtime,
    double refresh_min,
    double refresh_max
) {

    if (vtime == 0)
        return std::nullopt;
    const double htotal = static_cast<double>(m.hact + m.hfp + m.hs + m.hbp);
    const double vtotal = static_cast<double>(m.vact + m.vfp + m.vs + m.vbp);
    double fpix = kFabricClockMhz * htotal * vtotal;
    fpix /= static_cast<double>(vtime);

    if (fpix < kFpixMinMhz || fpix > kFpixMaxMhz)
        return std::nullopt;

    const float hz = kFabricClockHzF / static_cast<float>(vtime);
    if (refresh_min != 0.0 && hz < static_cast<float>(refresh_min))
        return std::nullopt;
    if (refresh_max != 0.0 && hz > static_cast<float>(refresh_max))
        return std::nullopt;
    return fpix;
}

std::optional<ScanrateLock> lock_scanrate(const Modeline& m, std::uint32_t vtime) noexcept {
    if (vtime == 0)
        return std::nullopt;
    const std::int64_t htotal = static_cast<std::int64_t>(m.hact) + m.hfp + m.hs + m.hbp;
    if (htotal <= 0 || !(m.fpix_mhz > 0.0))
        return std::nullopt;

    const std::int64_t vbp = static_cast<std::int64_t>(m.vbp);
    const std::int64_t min_vbp = (vbp < kScanlockMinVbp) ? vbp : kScanlockMinVbp;
    const std::int64_t vt_min = static_cast<std::int64_t>(m.vact) + m.vs + min_vbp + 1;

    const double vtotal_real = m.fpix_mhz
        * static_cast<double>(vtime)
        / (static_cast<double>(htotal) * kFabricClockMhz);

    if (!(vtotal_real >= 0.0 && vtotal_real < kScanlockVtotalRoundCeiling))
        return std::nullopt;
    const std::int64_t vtotal = std::llround(vtotal_real);
    if (vt_min > kScanlockVtotalMax || vtotal < vt_min)
        return std::nullopt;

    const std::int64_t vblank = vtotal - static_cast<std::int64_t>(m.vact) - m.vs;
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
    const std::uint32_t vfp = lock ? lock->vfp : m.vfp;
    const std::uint32_t vbp = lock ? lock->vbp : m.vbp;
    const std::int64_t vtotal = static_cast<std::int64_t>(m.vact) + vfp + m.vs + vbp;
    if (vtotal <= 0)
        return false;
    const std::int64_t vblank = vtotal - static_cast<std::int64_t>(m.vact);
    return (vblank * static_cast<std::int64_t>(s.vtime) / vtotal)
        > (static_cast<std::int64_t>(s.de_v) + kScanlockRotMargin)
        * static_cast<std::int64_t>(s.htime);
}

FamilyChoice select_family(
    std::uint32_t vtime,
    bool vsync_adjust,
    bool have_pal,
    bool have_ntsc
) noexcept {

    FamilyChoice out{};
    if (vtime == 0 || !vsync_adjust)
        return out;
    if (have_pal || have_ntsc) {
        if (vtime > kPalVtimeTicks) {
            out.family = have_pal ? VmodeFamily::Pal : VmodeFamily::Ntsc;
            out.adjustable = have_pal;
        } else {
            out.family = have_ntsc ? VmodeFamily::Ntsc : VmodeFamily::Pal;
            out.adjustable = have_ntsc;
        }
        return out;
    }
    out.adjustable = true;
    return out;
}

}

}
