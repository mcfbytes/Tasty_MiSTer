// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/error.h"

namespace mister::svc {

inline std::unexpected<Error> err(Errc c, std::uint16_t site, std::uint32_t detail = 0) {
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

}

}
