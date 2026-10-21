// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "proto/types.h"

namespace mister::svc {

enum class CddaOrder : std::uint8_t { LittleEndian, BigEndian };

using proto::JoyMask;
using proto::PlayerIndex;

inline constexpr unsigned kMaxPlayers = 6;

struct RumblePadSet {
    std::uint8_t players = 0;
    std::array<std::uint32_t, kMaxPlayers> pads{};
    friend constexpr bool operator==(const RumblePadSet&, const RumblePadSet&) = default;
};

struct TrackNumber {
    std::uint8_t v = 0;
    friend constexpr bool operator==(TrackNumber, TrackNumber) = default;
    friend constexpr auto operator<=>(TrackNumber, TrackNumber) = default;
};

struct TrackIndex {
    std::uint8_t v = 0;
    friend constexpr bool operator==(TrackIndex, TrackIndex) = default;
    friend constexpr auto operator<=>(TrackIndex, TrackIndex) = default;
};

struct Crc32 {
    std::uint32_t v = 0;
    friend constexpr bool operator==(Crc32, Crc32) = default;
};

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
    friend constexpr bool operator==(Generation, Generation) = default;
    friend constexpr auto operator<=>(Generation, Generation) = default;
};

}
