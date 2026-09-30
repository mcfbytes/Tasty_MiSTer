// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mister::proto {

enum class Ps2Stream : std::uint8_t {
    KeyboardFrame,
    KeyboardReply,
    MousePacket,
    MouseReply,
    kCount,
};

inline constexpr std::size_t kPs2FrameBytes = 8;

struct Ps2Frame {
    Ps2Stream stream = Ps2Stream::KeyboardFrame;
    std::uint8_t len = 0;
    std::array<std::uint8_t, kPs2FrameBytes> bytes{};
};

}  // namespace mister::proto
