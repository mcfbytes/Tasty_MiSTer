// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/subcode_interleaver.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace mister::svc {

void SubcodeInterleaver::shape(SubcodeBlock block) const noexcept {

    constexpr std::size_t kChannels = 8;
    constexpr std::size_t kChannelStride = kSubcodeBytes / kChannels;
    constexpr unsigned kChannelBits = 3;
    constexpr unsigned kTopBit = 7;
    constexpr unsigned kBitIndexMask = 7;

    TASTY_SEAT_BODY(SubcodeInterleaver);

    std::array<std::byte, kSubcodeBytes> channels{};
    for (std::size_t i = 0; i < kSubcodeBytes; ++i)
        channels[i] = block[i];
    for (std::size_t symbol = 0; symbol < kSubcodeBytes; ++symbol) {
        const std::size_t bit_in_byte = kTopBit - (symbol & kBitIndexMask);
        unsigned code = 0;
        for (std::size_t ch = 0; ch < kChannels; ++ch) {
            const std::byte source = channels[ch * kChannelStride + (symbol >> kChannelBits)];
            const unsigned bit = (std::to_integer<unsigned>(source) >> bit_in_byte) & 1u;
            code |= bit << (kTopBit - ch);
        }
        block[symbol] = static_cast<std::byte>(code);
    }
}

}
