// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/scanout_packet.h"

#include "infra/endian.h"

namespace mister::app::scanout {
namespace {

[[nodiscard]] std::uint16_t le16(std::span<const std::byte> b, std::size_t at) noexcept {
    return infra::load_le<std::uint16_t>(b.subspan(at).first<sizeof(std::uint16_t)>());
}

void put16(std::span<std::byte, kMaxPacketBytes> b, std::size_t at, std::uint16_t v) noexcept {
    infra::store_le(b.subspan(at).first<sizeof v>(), v);
}

[[nodiscard]] hal::SpiWord word_at(std::span<const std::byte> packet, std::size_t at) noexcept {
    return hal::SpiWord::load_le(packet.subspan(at).first<sizeof(hal::SpiWord)>());
}

void put_word(std::span<std::byte, kMaxPacketBytes> out, std::size_t at, hal::SpiWord w) noexcept {
    w.store_le(out.subspan(at).first<sizeof(hal::SpiWord)>());
}

[[nodiscard]] std::size_t payload_at(std::size_t i) noexcept {
    return kHeaderBytes + i * sizeof(hal::SpiWord);
}

bool admitted(hal::SpiWord opcode, std::uint16_t count) noexcept {
    for (const Command& c : kCommands)
        if (c.opcode == opcode && c.words == count)
            return true;
    return false;
}

}

std::optional<UioBurst> decode(std::span<const std::byte> packet) noexcept {
    if (packet.size() < kHeaderBytes || packet.size() > kMaxPacketBytes)
        return std::nullopt;
    if (le16(packet, kMagicAt) != kMagic)
        return std::nullopt;
    const std::uint16_t count = le16(packet, kCountAt);
    if (packet.size() != payload_at(count))
        return std::nullopt;
    const hal::SpiWord opcode = word_at(packet, kOpcodeAt);
    if (!admitted(opcode, count))
        return std::nullopt;
    UioBurst b{};
    b.tag = le16(packet, kTagAt);
    b.opcode = opcode;
    b.count = static_cast<std::uint8_t>(count);
    for (std::size_t i = 0; i < count; ++i)
        b.words[i] = word_at(packet, payload_at(i));
    return b;
}

std::span<const std::byte> encode_reply(
    const UioBurst& answered,
    std::span<std::byte, kMaxPacketBytes> out
) noexcept {
    if (answered.count > UioBurst::kMaxWords)
        return {};
    put16(out, kMagicAt, kMagic);
    put16(out, kTagAt, answered.tag);
    put_word(out, kOpcodeAt, answered.opcode);
    put16(out, kCountAt, answered.count);
    for (std::size_t i = 0; i < answered.count; ++i)
        put_word(out, payload_at(i), answered.words[i]);
    return out.first(payload_at(answered.count));
}

}
