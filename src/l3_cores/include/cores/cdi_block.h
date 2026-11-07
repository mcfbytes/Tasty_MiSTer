// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/cd_msf.h"
#include "cores/types.h"
#include "infra/endian.h"
#include "proto/block_geometry.h"
#include "svc/msf.h"
#include "svc/toc.h"

namespace mister::cores::cdi {

inline constexpr std::size_t kSectorBytes = svc::kCdDataSize;

inline constexpr std::size_t kQBytes = 24;
inline constexpr std::size_t kRwBytes = 192;
inline constexpr std::size_t kSubcodeBytes = kQBytes + kRwBytes;
inline constexpr std::size_t kBlockBytes = kSectorBytes + kSubcodeBytes;
inline constexpr std::uint32_t kBlockSize32{kBlockBytes};
inline constexpr std::size_t kRawSubcodeBytes = 96;

inline constexpr std::uint16_t kCrc16Poly = 0x1021;
inline constexpr std::uint16_t kCrc16TopBit = 0x8000;
inline constexpr unsigned kCrc16Mask = 0xFFFF;
inline constexpr std::size_t kCrcTableSize = 256;
inline constexpr unsigned kByteBits = 8;

consteval std::array<std::uint16_t, kCrcTableSize> make_crc_table() {
    std::array<std::uint16_t, kCrcTableSize> t{};
    for (unsigned i = 0; i < kCrcTableSize; ++i) {
        unsigned c = i << kByteBits;
        for (unsigned k = 0; k < kByteBits; ++k)
            c = (c & kCrc16TopBit) ? ((c << 1) ^ kCrc16Poly) : (c << 1);
        t[i] = static_cast<std::uint16_t>(c);
    }
    return t;
}

inline constexpr auto kCrcTable = make_crc_table();

constexpr std::uint8_t bcd(unsigned v) noexcept {
    return proto::PackedBcd::unmasked_byte(v);
}

constexpr std::uint8_t bcd(svc::TrackNumber n) noexcept {
    return n.bcd_byte();
}

struct QFrame {
    static constexpr std::size_t kWords = 12;
    enum class Field : std::uint8_t {
        Control = 0,
        Point = 1,
        Index = 2,
        Minute = 3,
        Second = 4,
        Frame = 5,
        Zero = 6,
        AMinute = 7,
        ASecond = 8,
        AFrame = 9,
        CrcHigh = 10,
        CrcLow = 11,
        kCount = 12,
    };

    std::array<std::uint8_t, kQBytes> bytes{};

    constexpr void set(Field f, unsigned v) noexcept {
        const std::size_t at = static_cast<std::size_t>(f) * sizeof(std::uint16_t);
        infra::store_be<std::uint16_t>(
            std::span<std::uint8_t, sizeof(std::uint16_t)>(
                bytes.data() + at,
                sizeof(std::uint16_t)
            ),
            static_cast<std::uint16_t>(v)
        );
    }

    constexpr void seal() noexcept {
        unsigned accum = 0;
        for (std::size_t i = 0; i < kWords; ++i) {
            const unsigned data = bytes[1 + i * 2];
            accum = ((accum << kByteBits) | data) ^ kCrcTable[accum >> kByteBits];
            accum &= kCrc16Mask;
        }
        set(Field::CrcHigh, (accum >> kByteBits) & 0xFFu);
        set(Field::CrcLow, accum & 0xFFu);
    }

    [[nodiscard]] constexpr std::span<const std::uint8_t, kQBytes> view() const noexcept {
        return bytes;
    }

    friend constexpr bool operator==(QFrame, QFrame) = default;
};

static_assert(sizeof(QFrame) == kQBytes);

constexpr std::uint32_t hunk_window(std::uint32_t hunk_bytes) noexcept {
    return hunk_bytes != 0 && hunk_bytes <= proto::kBlockStagingBytes ? hunk_bytes / kBlockSize32
                                                                      : 0;
}

inline constexpr std::array<std::uint8_t, 15> kBootHeader =
    {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x02, 0x16};

inline constexpr std::size_t kTocEntries = 306;
using TocEntry = std::array<std::uint8_t, 5>;

[[nodiscard]] QFrame compose_q(std::int32_t lba, const svc::Toc& toc) noexcept;

[[nodiscard]] QFrame compose_toc_q(std::int32_t lba, std::span<const TocEntry> entries) noexcept;

void widen_rw(
    std::span<const std::uint8_t, kRawSubcodeBytes> symbols,
    std::span<std::uint8_t, kRwBytes> rw
) noexcept;

std::size_t build_toc(
    const svc::Toc& toc,
    DiscType disc_type,
    std::span<TocEntry, kTocEntries> out
) noexcept;

[[nodiscard]] DiscType disc_type(const svc::Toc& toc, DiscType previous) noexcept;

}
