// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdint>

#include "svc/rw_interleaver.h"
#include "svc/sector_descrambler.h"

namespace mister::svc {
namespace {

unsigned unbcd(unsigned v) noexcept { return ((v & 0xF0u) >> 4) * 10 + (v & 0x0Fu); }

int header_lba(unsigned m, unsigned s, unsigned f) noexcept {
    return static_cast<int>(unbcd(m) * 75 * 60 + unbcd(s) * 75 + unbcd(f));
}

constexpr std::size_t kRwOffset = 24;
constexpr std::size_t kRwSymbols = 96;

}  // namespace

void SectorDescrambler::shape(proto::Lba lba,
                              std::span<std::byte, kCdFrameSize> frame) const noexcept {
    TASTY_SEAT_BODY(SectorDescrambler);
    auto at = [&](std::size_t i) { return std::to_integer<unsigned>(frame[i]); };

    if (at(0) != 0 || at(11) != 0) return;
    for (std::size_t i = 1; i < 11; ++i) {
        if (at(i) != 0xFF) return;
    }
    const auto want = static_cast<int>(lba.v);
    if (header_lba(at(12), at(13), at(14)) == want && at(15) == 2) return;
    const int scr = header_lba(at(12) ^ kSectorScramble[0], at(13) ^ kSectorScramble[1],
                               at(14) ^ kSectorScramble[2]);
    if (scr != want || (at(15) ^ kSectorScramble[3]) != 2) return;
    for (std::size_t i = 12; i < kCdDataSize; ++i)
        frame[i] ^= std::byte{kSectorScramble[i - 12]};
}

void RwInterleaver::shape(proto::Lba, std::span<std::byte, kCdFrameSize> frame) const noexcept {
    TASTY_SEAT_BODY(RwInterleaver);
    const auto tail = frame.subspan<kCdDataSize, kCdFrameSize - kCdDataSize>();
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

consteval unsigned scramble_sum() {
    unsigned s = 0;
    for (auto b : kSectorScramble)
        s += b;
    return s;
}
consteval unsigned scramble_weighted() {
    unsigned long long s = 0;
    for (std::size_t i = 0; i < kSectorScramble.size(); ++i)
        s += (i + 1) * kSectorScramble[i];
    return static_cast<unsigned>(s % 1000003u);
}
static_assert(kSectorScramble.size() == 2340 && kSectorScramble[0] == 0x01 &&
                  kSectorScramble[1] == 0x80 && kSectorScramble[2339] == 0x99,
              "cdi.cpp:559,705: s_sector_scramble opens 0x01 0x80 and closes 0x99");
static_assert(scramble_sum() == 300160 && scramble_weighted() == 595423,
              "cdi.cpp:557-705: every byte of s_sector_scramble");

}  // namespace mister::svc
