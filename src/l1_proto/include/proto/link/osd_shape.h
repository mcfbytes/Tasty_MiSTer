// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/osd_block.h"
#include "proto/types.h"

namespace mister::proto::link {

struct OsdShape {
    std::uint32_t v = 0;

    enum Shape : std::uint32_t { kRowShift = 16, kBytesMask = 0xFFFFu };

    [[nodiscard]] constexpr bool admits() const noexcept {
        const std::uint32_t rows = v >> kRowShift;
        const std::uint32_t bytes = v & kBytesMask;
        return bytes == hal::OsdBlock::kRowBytes
            && (rows == hal::OsdBlock::kCoreRows || rows == hal::OsdBlock::kImageRows);
    }

    [[nodiscard]] constexpr OsdRowCount rows() const noexcept {
        return OsdRowCount{v >> kRowShift};
    }

    [[nodiscard]] constexpr Error refusal(Errc why, infra::ErrorSite at) const noexcept {
        return Error{why, at, v};
    }

    friend constexpr bool operator==(OsdShape, OsdShape) = default;
};

}
