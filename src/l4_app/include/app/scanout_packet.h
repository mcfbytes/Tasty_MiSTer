// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "app/uio_burst.h"

namespace mister::app::scanout {

inline constexpr std::string_view kRequest = "ZAPAROO-SCANOUT-2";
inline constexpr std::string_view kGrant = "ZAPAROO-SCANOUT-2 PROXY";
inline constexpr std::string_view kRefuse = "ZAPAROO-SCANOUT-2 NO";

inline constexpr std::string_view kFdEnv = "ZAPAROO_SCANOUT_FD";

inline constexpr std::uint16_t kMagic = 0x5A52;
inline constexpr std::size_t kHeaderBytes = 8;
inline constexpr std::size_t kMaxPacketBytes = 32;

struct Command {
    std::uint16_t opcode;
    std::uint8_t words;
};
inline constexpr Command kCommands[] = {{0x57, 12}, {0x59, 6}, {0x5B, 11}};

[[nodiscard]] std::optional<UioBurst> decode(std::span<const std::byte> packet) noexcept;

[[nodiscard]] std::size_t encode_reply(const UioBurst& answered,
                                       std::span<std::byte, kMaxPacketBytes> out) noexcept;

}  // namespace mister::app::scanout
