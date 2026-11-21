// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "app/launcher/uio_burst.h"
#include "proto/uio_opcodes.h"

namespace mister::app::launcher::scanout {

inline constexpr std::string_view kRequest = "ZAPAROO-SCANOUT-2";
inline constexpr std::string_view kGrant = "ZAPAROO-SCANOUT-2 PROXY";
inline constexpr std::string_view kRefuse = "ZAPAROO-SCANOUT-2 NO";

inline constexpr std::string_view kFdEnv = "ZAPAROO_SCANOUT_FD";

inline constexpr std::uint16_t kMagic = 0x5A52;
inline constexpr std::size_t kHeaderBytes = 8;
inline constexpr std::size_t kMagicAt = 0, kTagAt = 2, kOpcodeAt = 4, kCountAt = 6;
static_assert(kCountAt + sizeof(std::uint16_t) == kHeaderBytes);
inline constexpr std::size_t kMaxPacketBytes = 32;

struct Command {
    hal::SpiWord opcode;
    std::uint8_t words;
};

inline constexpr auto kCommands = std::to_array<Command>(
    {{proto::word(proto::UioOp::Scanout57), 12},
     {proto::word(proto::UioOp::Scanout59), 6},
     {proto::word(proto::UioOp::Scanout5B), 11}}
);

[[nodiscard]] std::optional<UioBurst> decode(std::span<const std::byte> packet) noexcept;

[[nodiscard]] std::span<const std::byte> encode_reply(
    const UioBurst& answered,
    std::span<std::byte, kMaxPacketBytes> out
) noexcept;

}
