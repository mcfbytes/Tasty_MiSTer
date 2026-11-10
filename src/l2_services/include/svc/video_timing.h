// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/indexed_array.h"
#include "svc/modeline.h"
#include "svc/modeline_item.h"
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

inline constexpr std::size_t kNumTvmodes = 4;

std::span<const PresetMode> vmodes() noexcept;
std::span<const PresetMode> tvmodes() noexcept;

enum class VFilter : std::uint8_t { Horz = 0, Vert = 1, Scanlines = 2, Ilace = 3 };

inline constexpr std::size_t kScalerSlots = 4;

struct ScalerSlotIndex {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr ScalerSlotIndex of(VFilter f) noexcept {
        return ScalerSlotIndex{static_cast<std::uint8_t>(f)};
    }

    [[nodiscard]] constexpr std::size_t offset(std::size_t stride) const noexcept {
        return static_cast<std::size_t>(v) * stride;
    }

    friend constexpr bool operator==(ScalerSlotIndex, ScalerSlotIndex) = default;
};

template <class T>
using ScalerSlots = infra::IndexedArray<T, kScalerSlots, ScalerSlotIndex>;

inline constexpr std::array<ScalerSlotIndex, kScalerSlots> kScalerSlotOrder{
    ScalerSlotIndex::of(VFilter::Horz),
    ScalerSlotIndex::of(VFilter::Vert),
    ScalerSlotIndex::of(VFilter::Scanlines),
    ScalerSlotIndex::of(VFilter::Ilace)
};

static_assert(
    kScalerSlotOrder.size() == kScalerSlots
        && kScalerSlotOrder[0] == ScalerSlotIndex::of(VFilter::Horz)
        && kScalerSlotOrder[1] == ScalerSlotIndex::of(VFilter::Vert)
        && kScalerSlotOrder[2] == ScalerSlotIndex::of(VFilter::Scanlines)
        && kScalerSlotOrder[3] == ScalerSlotIndex::of(VFilter::Ilace),
    "the slot table lists every VFilter in the cfg blob's byte order"
);

enum class VmodeFamily : std::uint8_t { Default = 0, Pal, Ntsc };

inline constexpr std::uint32_t kPalVtimeTicks = 1'800'000;

inline constexpr std::int64_t kScanlockMinVbp = 6;
inline constexpr std::int64_t kScanlockVtotalMax = 4095;
inline constexpr std::int64_t kScanlockRotMargin = 5;

inline constexpr double kScanlockVtotalRoundCeiling = static_cast<double>(kScanlockVtotalMax) + 0.5;

namespace video_timing {

[[nodiscard]] Modeline preset_modeline(const PresetMode& p) noexcept;

[[nodiscard]] Modeline with_timings(
    Modeline m,
    std::span<const std::uint32_t, kModelineTimings> t
) noexcept;

struct CoreVideoCaps {
    bool full_hd = false;
    bool pixel_repeat = false;
};

StoredMode store_mode(const ParsedMode& parsed, CoreVideoCaps caps);

StoredMode preset_fallback(CoreVideoCaps caps);

VFilter select_vfilter(const VideoSample& s, ScalerSlots<bool> slot_enabled);

struct RefreshBand {
    double min_hz = 0.0;
    double max_hz = 0.0;

    [[nodiscard]] bool contains(float hz) const noexcept;
};

std::optional<double> estimate_fpix(const Modeline& m, std::uint32_t vtime, RefreshBand band);

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

struct FamilyConfig {
    bool switching = false;
    bool have_pal = false;
    bool have_ntsc = false;
};

[[nodiscard]] FamilyChoice select_family(std::uint32_t vtime, FamilyConfig cfg) noexcept;

}

}
