// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cdwire.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "svc/disc_engine.h"

namespace mister::cores {

namespace {

using svc::TrackType;

constexpr std::uint16_t lo16(std::uint64_t v) noexcept {
    constexpr std::uint64_t kWordMask = 0xFFFF;
    return static_cast<std::uint16_t>(v & kWordMask);
}

}

namespace cdwire {

std::uint64_t sega_compose(std::uint16_t w0, std::uint16_t w1, std::uint16_t w2) noexcept {
    constexpr unsigned kWordBits = 16;
    constexpr std::uint64_t kByteMask = 0xFF;
    constexpr unsigned kSegaWord1Shift = kWordBits;
    constexpr unsigned kSegaWord2Shift = 2 * kWordBits;

    return static_cast<std::uint64_t>(w0)
        | (static_cast<std::uint64_t>(w1) << kSegaWord1Shift)
        | (static_cast<std::uint64_t>(w2 & kByteMask) << kSegaWord2Shift);
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

std::array<std::uint16_t, 3> sega_set_words(std::uint64_t frame) noexcept {
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
        lo16(frame),
        lo16(frame >> kSegaWord1Shift),
        static_cast<std::uint16_t>(
            ((frame >> kSegaWord2Shift) & kByteMask)
            | (((frame >> kSegaIsDataShift) & 1u) << kWord2IsDataShift)
        )
    };
}

std::optional<std::uint16_t> sega_send_data_query(TrackType type, bool skip_mode1) noexcept {
    constexpr unsigned kSendDataTypeShift = 2;

    if (skip_mode1 && type == TrackType::Mode1)
        return std::nullopt;
    return static_cast<std::uint16_t>(
        kCddGetSendData
        | (static_cast<std::uint16_t>(static_cast<std::uint8_t>(type)) << kSendDataTypeShift)
    );
}

std::array<std::uint16_t, 2> pce_set_words(
    std::uint16_t status,
    bool region,
    bool data_request
) noexcept {
    constexpr std::uint16_t kPceRegionBit = 2;
    constexpr std::uint16_t kPceDataRequestBit = 1;

    return {
        status,
        static_cast<std::uint16_t>(
            (region ? kPceRegionBit : 0u) | (data_request ? kPceDataRequestBit : 0u)
        )
    };
}

std::array<std::uint8_t, 96> interleave_subcode(std::span<const std::uint8_t, 96> raw) noexcept {
    constexpr std::size_t kSubcodeBytes = 96;
    constexpr std::size_t kSubcodeChannels = 8;
    constexpr std::size_t kSubcodeChannelStride = 12;
    constexpr unsigned kChannelBits = 3;
    constexpr unsigned kTopBit = 7;
    constexpr unsigned kBitIndexMask = 7;

    std::array<std::uint8_t, 96> out{};
    for (std::size_t i = 0; i < kSubcodeBytes; ++i) {
        unsigned code = 0;
        for (std::size_t j = 0; j < kSubcodeChannels; ++j) {
            const unsigned bit = (static_cast<unsigned>(
                                      raw[j * kSubcodeChannelStride + (i >> kChannelBits)]
                                  )
                                  >> (kTopBit - (i & kBitIndexMask)))
                & 1u;
            code |= bit << (kTopBit - j);
        }
        out[i] = static_cast<std::uint8_t>(code);
    }
    return out;
}

}

}
