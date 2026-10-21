// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "svc/modeline.h"
#include "svc/parsed_mode.h"
#include "svc/stored_mode.h"
#include "svc/video_sample.h"

namespace mister::svc {

struct PresetMode {
    std::uint32_t vpar[8];
    double fpix_mhz;
    std::uint8_t vic;
    std::uint8_t pr;
};

inline constexpr std::size_t kNumVmodes = 15;
inline constexpr std::size_t kNumTvmodes = 4;

std::span<const PresetMode> vmodes() noexcept;
std::span<const PresetMode> tvmodes() noexcept;

enum class VFilter : std::uint8_t { Horz = 0, Vert = 1, Scanlines = 2, Ilace = 3 };

enum class VmodeFamily : std::uint8_t { Default = 0, Pal, Ntsc };

inline constexpr std::uint32_t kPalVtimeTicks = 1'800'000;

inline constexpr std::int64_t kScanlockMinVbp = 6;
inline constexpr std::int64_t kScanlockVtotalMax = 4095;
inline constexpr std::int64_t kScanlockRotMargin = 5;

inline constexpr double kScanlockVtotalRoundCeiling = static_cast<double>(kScanlockVtotalMax) + 0.5;

namespace video_timing {

StoredMode store_mode(const ParsedMode& parsed, bool support_fhd, bool supports_pr);

StoredMode preset_fallback(bool support_fhd, bool supports_pr);

VFilter select_vfilter(const VideoSample& s, std::span<const bool> slot_enabled);

std::optional<double> estimate_fpix(
    const Modeline& m,
    std::uint32_t vtime,
    double refresh_min,
    double refresh_max
);

struct ScanrateLock {
    std::uint32_t vfp = 0;
    std::uint32_t vbp = 0;
    std::uint32_t vtotal = 0;
    double target_scanrate_hz = 0.0;
    double actual_scanrate_hz = 0.0;
};

[[nodiscard]] std::optional<ScanrateLock> lock_scanrate(
    const Modeline& m,
    std::uint32_t vtime
) noexcept;

[[nodiscard]] bool rotation_needs_buffered_scaler(
    const Modeline& m,
    const std::optional<ScanrateLock>& lock,
    const VideoSample& s
) noexcept;

struct FamilyChoice {
    VmodeFamily family = VmodeFamily::Default;
    bool adjustable = false;
};

[[nodiscard]] FamilyChoice select_family(
    std::uint32_t vtime,
    bool vsync_adjust,
    bool have_pal,
    bool have_ntsc
) noexcept;

}

}
