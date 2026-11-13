// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>

#include "svc/msf.h"
#include "svc/sector_descrambler.h"

namespace mister::svc {

void SectorDescrambler::shape(
    proto::Lba lba,
    std::span<std::byte, kFullFrameBytes> frame
) const noexcept {
    TASTY_SEAT_BODY(SectorDescrambler);
    const auto names = [lba](std::span<const std::byte, SectorHeader::kBytes> bytes) {
        const std::optional<SectorHeader> h = SectorHeader::read(bytes);
        return h && h->mode == SectorHeader::Mode::Mode2 && h->lba() == lba;
    };
    const std::span<std::byte, SectorHeader::kBytes> head = frame.first<SectorHeader::kBytes>();
    if (names(head))
        return;
    std::array<std::byte, SectorHeader::kBytes> plain{};
    std::ranges::copy(head, plain.begin());
    for (std::size_t i = SectorHeader::kSync.size(); i < plain.size(); ++i)
        plain[i] ^= std::byte{kSectorScramble[i - SectorHeader::kSync.size()]};
    if (!names(plain))
        return;
    for (std::size_t i = SectorHeader::kSync.size(); i < kRawFrameBytes; ++i)
        frame[i] ^= std::byte{kSectorScramble[i - SectorHeader::kSync.size()]};
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
