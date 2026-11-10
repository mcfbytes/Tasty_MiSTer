// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <span>

#include "svc/rw_interleaver.h"

namespace mister::svc {
namespace {

constexpr std::size_t kRwOffset = 24;
constexpr std::size_t kRwSymbols = 96;

}

void RwInterleaver::shape(proto::Lba, std::span<std::byte, kFullFrameBytes> frame) const noexcept {
    TASTY_SEAT_BODY(RwInterleaver);
    const auto tail = frame.subspan<kRawFrameBytes, kFullFrameBytes - kRawFrameBytes>();
    std::byte channels[kRwSymbols - kRwOffset];
    for (std::size_t i = 0; i < sizeof(channels); ++i)
        channels[i] = tail[kRwOffset + i];
    for (std::size_t symbol = 0; symbol < kRwSymbols; ++symbol) {
        unsigned out = 0;
        for (std::size_t ch = 0; ch < 6; ++ch) {
            const unsigned b = std::to_integer<unsigned>(channels[ch * 12 + (symbol >> 3)]);
            out |= ((b >> (7 - (symbol & 7))) & 1u) << (5 - ch);
        }
        tail[symbol] = static_cast<std::byte>(out);
    }
}

}
