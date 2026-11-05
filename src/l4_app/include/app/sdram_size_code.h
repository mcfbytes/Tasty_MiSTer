// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "proto/types.h"

namespace mister::app {

inline constexpr proto::OsdMask kSdramCookieMeasured = proto::kSdramPresent;

enum class SdramSizeCode : std::uint8_t { Unwired, Mb32, Mb64, Mb128 };

[[nodiscard]] constexpr std::uint16_t sdram_word(SdramSizeCode code, bool measured) noexcept {
    const proto::OsdMask size{static_cast<std::uint16_t>(code)};
    return (measured ? kSdramCookieMeasured.with(size) : size).v;
}

[[nodiscard]] constexpr SdramSizeCode sdram_size_code(proto::OsdMask osd_mask) noexcept {
    const proto::OsdMask size = osd_mask.only(proto::kSdramSizeBits);
    if (size == proto::kSdramSize128)
        return SdramSizeCode::Mb128;
    if (size == proto::kSdramSize64)
        return SdramSizeCode::Mb64;
    if (size == proto::kSdramSize32)
        return SdramSizeCode::Mb32;
    return SdramSizeCode::Unwired;
}

static_assert(
    sdram_size_code(proto::OsdMask{7}) == SdramSizeCode::Mb128
    && sdram_size_code(proto::OsdMask{3}) == SdramSizeCode::Mb64
    && sdram_size_code(proto::OsdMask{1}) == SdramSizeCode::Mb32
    && sdram_size_code(proto::OsdMask{0}) == SdramSizeCode::Unwired
    && sdram_size_code(proto::OsdMask{2}) == SdramSizeCode::Unwired
    && sdram_size_code(proto::OsdMask{4}) == SdramSizeCode::Unwired
    && sdram_size_code(proto::OsdMask{5}) == SdramSizeCode::Unwired
    && sdram_size_code(proto::OsdMask{6}) == SdramSizeCode::Unwired
);
static_assert(
    sdram_word(SdramSizeCode::Mb128, false) == 3
    && sdram_word(SdramSizeCode::Mb64, false) == 2
    && sdram_word(SdramSizeCode::Mb32, false) == 1
    && sdram_word(SdramSizeCode::Unwired, true) == kSdramCookieMeasured.v
);
static_assert(!kSdramCookieMeasured.has(proto::kSdramSizeBits));

}
