// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/byte_offset.h"

namespace mister::cores::atari8 {

struct CartType {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr CartType of_header(
        std::span<const std::uint8_t, 8> head
    ) noexcept {
        return CartType{head[kHeaderByte.bytes()]};
    }

    friend constexpr bool operator==(CartType, CartType) = default;

    static constexpr infra::ByteOffset kHeaderByte{7};
};

}
