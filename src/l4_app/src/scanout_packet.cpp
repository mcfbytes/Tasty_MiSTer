// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/scanout_packet.h"

namespace mister::app::scanout {
namespace {

std::uint16_t le16(std::span<const std::byte> b, std::size_t at) noexcept {
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(b[at]) |
                                      (std::to_integer<unsigned>(b[at + 1]) << 8));
}

void put16(std::span<std::byte, kMaxPacketBytes> b, std::size_t at, std::uint16_t v) noexcept {
    b[at] = static_cast<std::byte>(v & 0xFFu);
    b[at + 1] = static_cast<std::byte>(v >> 8);
}

bool admitted(std::uint16_t opcode, std::uint16_t count) noexcept {
    for (const Command& c : kCommands)
        if (c.opcode == opcode && c.words == count) return true;
    return false;
}

}  // namespace

std::optional<UioBurst> decode(std::span<const std::byte> packet) noexcept {
    if (packet.size() < kHeaderBytes || packet.size() > kMaxPacketBytes) return std::nullopt;
    if (le16(packet, 0) != kMagic) return std::nullopt;
    const std::uint16_t count = le16(packet, 6);
    if (packet.size() != kHeaderBytes + 2u * count) return std::nullopt;
    const std::uint16_t opcode = le16(packet, 4);
    if (!admitted(opcode, count)) return std::nullopt;
    UioBurst b{};
    b.tag = le16(packet, 2);
    b.opcode = opcode;
    b.count = static_cast<std::uint8_t>(count);
    for (std::size_t i = 0; i < count; ++i)
        b.words[i] = le16(packet, kHeaderBytes + 2u * i);
    return b;
}

std::size_t encode_reply(const UioBurst& answered,
                         std::span<std::byte, kMaxPacketBytes> out) noexcept {
    if (answered.count > UioBurst::kMaxWords) return 0;
    put16(out, 0, kMagic);
    put16(out, 2, answered.tag);
    put16(out, 4, answered.opcode);
    put16(out, 6, answered.count);
    for (std::size_t i = 0; i < answered.count; ++i)
        put16(out, kHeaderBytes + 2u * i, answered.words[i]);
    return kHeaderBytes + 2u * answered.count;
}

}  // namespace mister::app::scanout
