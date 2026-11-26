// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/link/pad_event.h"
#include "proto/link/pad_key_image.h"
#include "proto/link/pad_mouse_stores.h"

#include <cstddef>
#include <cstdint>
#include <optional>

#include "proto/ps2_wire.h"

namespace mister::proto::link {

namespace {

constexpr std::uint8_t kSkipByte = 0xFF;
constexpr unsigned kByteBits = 8;
constexpr unsigned kThirdByteShift = 2 * kByteBits;

constexpr std::size_t kPacketWords = 3;

constexpr std::uint32_t kWheelMask = 0x7F;
constexpr std::uint32_t kWheelSign = 0x40;
constexpr std::uint32_t kNibbleMask = 0x0F;
constexpr unsigned kExtWord2Shift = 8;
constexpr unsigned kExtWord3Shift = 12;

std::size_t latched_words(const Ps2Frame& f, UioOp op) noexcept {
    const std::size_t n = ps2_frame_word_count(f);
    if (n == 0 || ps2_stream_facts(f.stream).opcode != op)
        return 0;
    for (std::size_t i = 0; i < n; ++i)
        if (ps2_frame_word(f, i).high_byte() == kSkipByte)
            return 0;
    return n;
}

}

std::optional<PadKeyImage> PadKeyImage::of(const Ps2Frame& f) noexcept {
    const std::size_t n = latched_words(f, UioOp::Keyboard);
    if (n == 0)
        return std::nullopt;
    std::uint32_t raw = 0;
    for (std::size_t i = 0; i < n; ++i)
        raw = (raw << kByteBits) | std::uint32_t{ps2_frame_word(f, i).low_byte()};
    return PadKeyImage{raw};
}

std::optional<PadMouseStores> PadMouseStores::of(const Ps2Frame& f) noexcept {
    if (latched_words(f, UioOp::Mouse) < kPacketWords)
        return std::nullopt;
    const hal::LinkWord w1 = ps2_frame_word(f, 0);
    const hal::LinkWord w2 = ps2_frame_word(f, 1);
    const hal::LinkWord w3 = ps2_frame_word(f, 2);
    const std::uint32_t packet = std::uint32_t{w1.low_byte()}
        | (std::uint32_t{w2.low_byte()} << kByteBits)
        | (std::uint32_t{w3.low_byte()} << kThirdByteShift);
    const std::uint32_t wheel = w1.high_byte();
    const std::uint32_t sign = (wheel & kWheelSign) != 0u ? 1u << (kByteBits - 1) : 0u;
    const std::uint32_t ext = (wheel & kWheelMask)
        | sign
        | ((std::uint32_t{w2.high_byte()} & kNibbleMask) << kExtWord2Shift)
        | ((std::uint32_t{w3.high_byte()} & kNibbleMask) << kExtWord3Shift);
    PadMouseStores stores;
    stores.ext_ = ext;
    stores.packet_ = packet;
    return stores;
}

std::optional<PadEvent> PadEvent::of(const Ps2Frame& f) noexcept {
    if (const auto m = PadMouseStores::of(f))
        return PadEvent{*m};
    if (const auto k = PadKeyImage::of(f))
        return PadEvent{*k};
    return std::nullopt;
}

}
