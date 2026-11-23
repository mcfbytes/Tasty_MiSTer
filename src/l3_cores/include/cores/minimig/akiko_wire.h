// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

#include "cores/minimig/akiko_opcode.h"
#include "infra/byte_count.h"
#include "proto/types.h"
#include "proto/user_io/head_lengths.h"

namespace mister::cores::minimig::akiko {

inline constexpr std::size_t kFrameMax = 32;
using Frame = std::array<std::byte, kFrameMax + 1>;

[[nodiscard]] consteval proto::user_io::HeadLengths drain_lengths() noexcept {
    proto::user_io::HeadLengths t{};
    for (std::size_t op = 0; op < t.more.size(); ++op)
        t.more[op] = payload_of(kOpcodes[op]).value_or(proto::ReplyCount{});
    return t;
}

static_assert(drain_lengths().widest() == proto::ReplyCount{12});

inline constexpr std::string_view kFirmware = "CHINON  O-658-2 24";

struct FrameSum {
    std::uint8_t v = 0;

    static const FrameSum kWhole;

    [[nodiscard]] static constexpr FrameSum of(std::span<const std::byte> bytes) noexcept {
        std::uint8_t sum = 0;
        for (const std::byte b : bytes)
            sum = static_cast<std::uint8_t>(sum + std::to_integer<std::uint8_t>(b));
        return FrameSum{sum};
    }

    [[nodiscard]] constexpr bool holds() const noexcept { return v == kWhole.v; }

    [[nodiscard]] constexpr std::byte closing() const noexcept {
        return std::byte{static_cast<std::uint8_t>(kWhole.v - v)};
    }

    friend constexpr bool operator==(FrameSum, FrameSum) = default;
};

inline constexpr FrameSum FrameSum::kWhole{std::numeric_limits<std::uint8_t>::max()};

[[nodiscard]] constexpr infra::ByteCount framed(
    std::span<const std::byte> payload,
    Frame& out
) noexcept {
    if (payload.empty() || payload.size() > kFrameMax)
        return infra::ByteCount{};
    std::ranges::copy(payload, out.begin());
    out[payload.size()] = FrameSum::of(payload).closing();
    return infra::ByteCount{payload.size() + 1};
}

}
