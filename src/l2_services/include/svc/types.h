// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <span>

#include "infra/affine.h"
#include "proto/types.h"

namespace mister::svc {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator-;
using ::mister::infra::affine::operator*;
using ::mister::infra::affine::operator+=;
using ::mister::infra::affine::operator-=;

enum class CddaOrder : std::uint8_t { LittleEndian, BigEndian };

using proto::JoyMask;
using proto::PlayerIndex;

inline constexpr unsigned kMaxPlayers = 6;

struct PlayerMask {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr PlayerMask with(PlayerIndex player) const noexcept {
        PlayerMask out = *this;
        for (std::uint8_t i = 0; i < kMaxPlayers; ++i) {
            if (PlayerIndex{i} == player)
                out.v = static_cast<std::uint8_t>(v | (1u << i));
        }
        return out;
    }

    [[nodiscard]] constexpr bool has(PlayerIndex player) const noexcept {
        for (std::uint8_t i = 0; i < kMaxPlayers; ++i) {
            if (PlayerIndex{i} == player)
                return (v >> i & 1u) != 0;
        }
        return false;
    }

    friend constexpr bool operator==(PlayerMask, PlayerMask) = default;
};

static_assert(kMaxPlayers <= 8, "PlayerMask holds one bit per player slot");

struct RumblePadSet {
    PlayerMask players{};
    std::array<std::uint32_t, kMaxPlayers> pads{};
    friend constexpr bool operator==(const RumblePadSet&, const RumblePadSet&) = default;
};

struct TrackNumber {
    std::uint8_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr TrackNumber of(I n) noexcept {
        return TrackNumber{static_cast<std::uint8_t>(n)};
    }

    friend constexpr bool operator==(TrackNumber, TrackNumber) = default;
    friend constexpr auto operator<=>(TrackNumber, TrackNumber) = default;
};

struct TrackIndex {
    std::uint8_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr TrackIndex of(I i) noexcept {
        return TrackIndex{static_cast<std::uint8_t>(i)};
    }

    friend constexpr bool operator==(TrackIndex, TrackIndex) = default;
    friend constexpr auto operator<=>(TrackIndex, TrackIndex) = default;
};

struct Crc32 {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr std::array<std::uint8_t, 4> to_be_bytes() const noexcept {
        return std::bit_cast<std::array<std::uint8_t, 4>>(
            std::endian::native == std::endian::little ? std::byteswap(v) : v
        );
    }

    friend constexpr bool operator==(Crc32, Crc32) = default;
};

[[nodiscard]] Crc32 crc32_extend(Crc32 running, std::span<const std::uint8_t> data) noexcept;

struct Vid {
    std::uint16_t v = 0;
    friend constexpr bool operator==(Vid, Vid) = default;
};

struct Pid {
    std::uint16_t v = 0;
    friend constexpr bool operator==(Pid, Pid) = default;
};

inline constexpr Vid kAnyVid{0};
inline constexpr Pid kAnyPid{0};

struct Generation {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr Generation next() const noexcept { return Generation{v + 1}; }

    friend constexpr bool operator==(Generation, Generation) = default;
    friend constexpr auto operator<=>(Generation, Generation) = default;
};

enum class VscaleMode : std::uint8_t {
    Fit,
    Integer,
    HalfStep,
    QuarterStep,
    ResolutionCoreAspect,
    ResolutionDisplayAspect
};

struct OpenFileId {
    std::uint16_t v = 0;
    friend constexpr bool operator==(OpenFileId, OpenFileId) = default;
};

namespace adv7513 {

enum class HdrMode : std::uint8_t { Off, Hlg, DciP3 };
}

}
