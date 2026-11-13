// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "proto/uio_opcodes.h"

namespace mister::proto {

class OpBitmap {
public:
    constexpr OpBitmap() noexcept = default;

    [[nodiscard]] static constexpr OpBitmap of(const std::array<std::uint32_t, 8>& words) noexcept {
        OpBitmap b;
        b.words_ = words;
        return b;
    }

    [[nodiscard]] constexpr bool honours(UioOp op) const noexcept {
        const auto o = proto::word(op).v;
        constexpr std::uint32_t kWordBits = std::numeric_limits<std::uint32_t>::digits;
        return o <= std::numeric_limits<std::uint8_t>::max()
            && ((words_[o / kWordBits] >> (o % kWordBits)) & 1u) != 0u;
    }

    friend constexpr bool operator==(const OpBitmap&, const OpBitmap&) = default;

private:
    std::array<std::uint32_t, 8> words_{};
};

static_assert(std::is_trivially_copyable_v<OpBitmap> && sizeof(OpBitmap) == 32);

}
