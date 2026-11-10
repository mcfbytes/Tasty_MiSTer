// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <cstddef>
#include <span>

#include "svc/msf.h"
#include "svc/sector_descrambler.h"

namespace mister::svc {
namespace {

constexpr unsigned kMode2 = 2;

}

void SectorDescrambler::shape(
    proto::Lba lba,
    std::span<std::byte, kFullFrameBytes> frame
) const noexcept {
    TASTY_SEAT_BODY(SectorDescrambler);
    auto at = [&](std::size_t i) {
        return std::to_integer<unsigned>(frame[i]);
    };

    if (at(0) != 0 || at(11) != 0)
        return;
    for (std::size_t i = 1; i < 11; ++i) {
        if (at(i) != 0xFF)
            return;
    }
    auto names = [&](std::span<const std::byte, 3> position) {
        return Msf::lba_from_bcd(position) == lba;
    };
    if (names(frame.subspan<12, 3>()) && at(15) == kMode2)
        return;
    std::array<std::byte, 3> plain{};
    for (std::size_t i = 0; i < plain.size(); ++i)
        plain[i] = frame[12 + i] ^ std::byte{kSectorScramble[i]};
    if (!names(plain) || (at(15) ^ kSectorScramble[3]) != kMode2)
        return;
    for (std::size_t i = 12; i < kRawFrameBytes; ++i)
        frame[i] ^= std::byte{kSectorScramble[i - 12]};
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

static_assert(
    kSectorScramble.size() == 2340
        && kSectorScramble[0] == 0x01
        && kSectorScramble[1] == 0x80
        && kSectorScramble[2339] == 0x99,
    "cdi.cpp:559,705: s_sector_scramble opens 0x01 0x80 and closes 0x99"
);
static_assert(
    scramble_sum() == 300160 && scramble_weighted() == 595423,
    "cdi.cpp:557-705: every byte of s_sector_scramble"
);

}
