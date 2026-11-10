// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/data_sector_sealer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace mister::svc {

namespace {

constexpr std::size_t kModeByteAt = 15;
constexpr std::uint8_t kModeTwo = 2;

constexpr std::uint32_t kEdcPoly = 0xD801'8001u;
constexpr std::size_t kTableEntries = 256;
constexpr unsigned kTableBits = 8;
constexpr unsigned kByteBits = 8;
constexpr std::uint32_t kByteMask = 0xFFu;

constexpr std::array<std::uint32_t, kTableEntries> make_edc_table() noexcept {
    std::array<std::uint32_t, kTableEntries> table{};
    for (std::uint32_t i = 0; i < kTableEntries; ++i) {
        std::uint32_t c = i;
        for (unsigned bit = 0; bit < kTableBits; ++bit)
            c = (c >> 1) ^ ((c & 1u) != 0u ? kEdcPoly : 0u);
        table[i] = c;
    }
    return table;
}

constexpr std::array<std::uint32_t, kTableEntries> kEdcTable = make_edc_table();

}

std::uint32_t DataSectorSealer::checksum(std::span<const std::byte> payload) noexcept {
    std::uint32_t crc = 0;
    for (const std::byte b : payload) {
        crc ^= std::to_integer<std::uint8_t>(b);
        crc = (crc >> kByteBits) ^ kEdcTable[crc & kByteMask];
    }
    return crc;
}

void DataSectorSealer::seal(std::span<std::byte, kRawFrameBytes> sector) const noexcept {
    TASTY_SEAT_BODY(DataSectorSealer);

    if (std::to_integer<std::uint8_t>(sector[kModeByteAt]) == kModeTwo)
        return;
    const std::uint32_t crc = checksum(sector.first(kModeOneEdcSpan));
    for (unsigned i = 0; i < kEdcBytes; ++i) {
        const unsigned shift = kByteBits * i;
        sector[kModeOneEdcSpan + i] = static_cast<std::byte>((crc >> shift) & kByteMask);
    }
    std::ranges::fill(sector.subspan(kModeOneTailFirst), std::byte{0});
}

}
