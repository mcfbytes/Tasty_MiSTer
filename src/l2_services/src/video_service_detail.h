// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"

namespace mister::svc {

inline std::unexpected<Error> err(Errc c, infra::ErrorSite site, std::uint32_t detail = 0) {
    return std::unexpected(Error{c, site, detail});
}

namespace {

constexpr double kFvcoStepMhz = static_cast<double>(50.f);
constexpr double kFvcoMaxMhz = static_cast<double>(1500.f);
constexpr double kKoBandLow = static_cast<double>(0.05f);
constexpr double kKoBandHigh = static_cast<double>(0.95f);
constexpr double kFpixRbRetryMhz = static_cast<double>(210.f);
constexpr double kFpixMinMhz = static_cast<double>(2.f);
constexpr double kFpixMaxMhz = static_cast<double>(300.f);

static_assert(kKoBandLow != 0.05);
static_assert(kKoBandHigh != 0.95);
static_assert(
    kFvcoStepMhz == 50.0
    && kFvcoMaxMhz == 1500.0
    && kFpixRbRetryMhz == 210.0
    && kFpixMinMhz == 2.0
    && kFpixMaxMhz == 300.0
);

constexpr std::uint32_t kSearchCeiling = 1u << 20;

constexpr int kCellGranRnd = 4;

constexpr float kPercentScale = 100.0f;

constexpr float kCvtRbMinVBlankUs = 460.0f;
constexpr float kCvtMinVSyncBackPorchUs = 550.0f;
constexpr int kCvtRbHBlank = 160;
constexpr int kCvtRbHSync = 32;
constexpr int kCvtRbHBackPorch = 80;
constexpr int kCvtMinVBporch = 6;
constexpr int kCvtVFrontPorch = 3;
constexpr int kCvtRbVFrontPorch = 3;

constexpr float kCvtCPrime = 30.0f;
constexpr float kCvtMPrime = 300.0f;
constexpr float kCvtHSyncPct = 0.08f;
constexpr float kCvtMinIdealDutyPct = 20.0f;

constexpr std::uint32_t kCvtPixelRepeatWidth = 2048;
constexpr std::uint32_t kCvtFallbackWidth = 1920;
constexpr std::uint32_t kCvtFallbackLines = 1080;

constexpr int kCvtDefaultVSync = 10;

struct AspectRule {
    int arx;
    int ary;
    int vsync;

    [[nodiscard]] constexpr int width_at(int h) const noexcept {
        return ((h * arx) / (ary * kCellGranRnd)) * kCellGranRnd;
    }
};

constexpr AspectRule kCvtAspectRules[] = {
    {4, 3, 4},
    {16, 9, 5},
    {16, 10, 6},
    {5, 4, 7},
    {15, 9, 7},
};

}

}
