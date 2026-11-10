// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "infra/seat.h"
#include "proto/link_op.h"

namespace mister::cores {

struct WireChunk {
    static constexpr std::uint16_t kOneWrite = 0;

    static constexpr std::size_t kBytesPerWideWord = 2;
    std::uint16_t v = kOneWrite;

    [[nodiscard]] static constexpr WireChunk single() noexcept { return WireChunk{}; }

    [[nodiscard]] static constexpr WireChunk of(std::size_t n) noexcept {
        constexpr std::uint16_t kMax = std::numeric_limits<std::uint16_t>::max();
        if (n == 0)
            return WireChunk{};
        return WireChunk{n > kMax ? kMax : static_cast<std::uint16_t>(n)};
    }

    [[nodiscard]] constexpr bool one_write() const noexcept { return v == kOneWrite; }

    [[nodiscard]] constexpr WireChunk per_byte_word() const noexcept {
        return of(kBytesPerWideWord * v);
    }

    [[nodiscard]] constexpr std::size_t whole_within(std::size_t cap) const noexcept {
        if (v >= cap)
            return v;
        return cap - cap % v;
    }

    constexpr void stamp(proto::LinkOp::StagePayload& op) const noexcept { op.chunk = v; }

    friend constexpr bool operator==(WireChunk, WireChunk) = default;
};

}
