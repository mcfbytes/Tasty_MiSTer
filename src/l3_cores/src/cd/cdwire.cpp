// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd/cdwire.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/endian.h"
#include "svc/disc/track.h"

namespace mister::cores::cd {

namespace {

using svc::disc::TrackType;

constexpr std::uint16_t lo16(std::uint64_t v) noexcept {
    constexpr std::uint64_t kWordMask = 0xFFFF;
    return static_cast<std::uint16_t>(v & kWordMask);
}

}

namespace cdwire {

std::uint64_t sega_compose(std::span<const hal::SpiWord, 3> words) noexcept {
    std::array<hal::SpiWord, 3> triple;
    std::ranges::copy(words, triple.begin());
    const auto& [w0, w1, w2] = triple;

    const std::array<std::uint8_t, sizeof(std::uint64_t)>
        frame{w0.low_byte(), w0.high_byte(), w1.low_byte(), w1.high_byte(), w2.low_byte()};
    return infra::load_le<std::uint64_t>(
        std::span<const std::uint8_t, sizeof(std::uint64_t)>(frame)
    );
}

CdSpeedIndex sega_speed_index(hal::SpiWord w2) noexcept {
    constexpr std::uint8_t kSegaSpeedMask = 0x3;
    const std::uint8_t index = w2.high_byte() & kSegaSpeedMask;
    return CdSpeedIndex{index};
}

std::uint8_t sega_crc(std::span<const std::uint8_t, 9> nibbles, std::uint8_t crc_start) noexcept {
    constexpr std::uint64_t kNibbleMask = 0xF;
    std::uint32_t sum = crc_start;
    for (const std::uint8_t n : nibbles)
        sum += n;
    return static_cast<std::uint8_t>(~sum & kNibbleMask);
}

bool sega_command_ok(std::uint64_t c, std::uint8_t crc_start) noexcept {
    constexpr unsigned kNibbleBits = 4;
    constexpr std::uint64_t kNibbleMask = 0xF;
    constexpr std::size_t kSegaNibbles = 9;
    constexpr unsigned kSegaCrcShift = kSegaNibbles * kNibbleBits;
    std::uint8_t nib[kSegaNibbles];
    for (std::size_t i = 0; i < kSegaNibbles; ++i) {
        nib[i] = static_cast<std::uint8_t>((c >> (kNibbleBits * i)) & kNibbleMask);
    }
    const auto crc = sega_crc(std::span<const std::uint8_t, 9>(nib), crc_start);
    return crc == static_cast<std::uint8_t>((c >> kSegaCrcShift) & kNibbleMask);
}

std::uint64_t sega_pack_status(
    std::span<const std::uint8_t, 9> stat,
    bool is_data,
    std::uint8_t crc_start,
    bool carries_is_data
) noexcept {
    constexpr unsigned kNibbleBits = 4;
    constexpr std::uint64_t kNibbleMask = 0xF;
    constexpr std::size_t kSegaNibbles = 9;
    constexpr unsigned kSegaCrcShift = kSegaNibbles * kNibbleBits;
    constexpr unsigned kSegaIsDataShift = kSegaCrcShift + kNibbleBits;

    std::uint64_t v = 0;
    for (std::size_t i = 0; i < kSegaNibbles; ++i) {
        v |= static_cast<std::uint64_t>(stat[i] & kNibbleMask) << (kNibbleBits * i);
    }
    v |= static_cast<std::uint64_t>(sega_crc(stat, crc_start)) << kSegaCrcShift;
    if (is_data && carries_is_data)
        v |= (std::uint64_t{1} << kSegaIsDataShift);
    return v;
}

std::array<hal::SpiWord, 3> sega_set_words(std::uint64_t frame) noexcept {
    constexpr unsigned kWordBits = 16;
    constexpr std::uint64_t kByteMask = 0xFF;
    constexpr unsigned kNibbleBits = 4;
    constexpr std::size_t kSegaNibbles = 9;
    constexpr unsigned kSegaWord1Shift = kWordBits;
    constexpr unsigned kSegaWord2Shift = 2 * kWordBits;
    constexpr unsigned kSegaCrcShift = kSegaNibbles * kNibbleBits;
    constexpr unsigned kSegaIsDataShift = kSegaCrcShift + kNibbleBits;
    constexpr unsigned kWord2IsDataShift = 8;

    return {
        hal::SpiWord{lo16(frame)},
        hal::SpiWord{lo16(frame >> kSegaWord1Shift)},
        hal::SpiWord{static_cast<std::uint16_t>(
            ((frame >> kSegaWord2Shift) & kByteMask)
            | (((frame >> kSegaIsDataShift) & 1u) << kWord2IsDataShift)
        )}
    };
}

std::optional<Selector> sega_send_data_query(TrackType type, bool skip_mode1) noexcept {

    if (skip_mode1 && type == TrackType::Mode1)
        return std::nullopt;
    return Selector::send_data_query(type);
}

std::array<hal::SpiWord, 2> pce_set_words(
    std::uint16_t status,
    bool region,
    bool data_request
) noexcept {
    constexpr std::uint16_t kPceRegionBit = 2;
    constexpr std::uint16_t kPceDataRequestBit = 1;

    return {
        hal::SpiWord{status},
        hal::SpiWord{static_cast<std::uint16_t>(
            (region ? kPceRegionBit : 0u) | (data_request ? kPceDataRequestBit : 0u)
        )}
    };
}

}

}
