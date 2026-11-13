// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace mister::svc::adv7513 {

inline constexpr std::size_t kSpdPacketBytes = 31;

struct SpdPacket {
    std::array<std::uint8_t, kSpdPacketBytes> b{};
    friend bool operator==(const SpdPacket&, const SpdPacket&) = default;
};

}
