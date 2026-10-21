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
    return static_cast<std::uint16_t>(v & 0xFFFFu);
}

}

namespace cdwire {

std::uint64_t sega_compose(std::uint16_t w0, std::uint16_t w1, std::uint16_t w2) noexcept {

    return static_cast<std::uint64_t>(w0)
        | (static_cast<std::uint64_t>(w1) << 16)
        | (static_cast<std::uint64_t>(w2 & 0xFFu) << 32);
}

std::uint8_t sega_crc(std::span<const std::uint8_t, 9> nibbles, std::uint8_t crc_start) noexcept {
    std::uint32_t sum = crc_start;
    for (const std::uint8_t n : nibbles)
        sum += n;
    return static_cast<std::uint8_t>(~sum & 0xFu);
}

bool sega_command_ok(std::uint64_t c, std::uint8_t crc_start) noexcept {
    std::uint8_t nib[9];
    for (std::size_t i = 0; i < 9; ++i) {
        nib[i] = static_cast<std::uint8_t>((c >> (4 * i)) & 0xFu);
    }
    const auto crc = sega_crc(std::span<const std::uint8_t, 9>(nib), crc_start);
    return crc == static_cast<std::uint8_t>((c >> 36) & 0xFu);
}

std::uint64_t sega_pack_status(
    std::span<const std::uint8_t, 9> stat,
    bool is_data,
    std::uint8_t crc_start,
    bool carries_is_data
) noexcept {

    std::uint64_t v = 0;
    for (std::size_t i = 0; i < 9; ++i) {
        v |= static_cast<std::uint64_t>(stat[i] & 0xFu) << (4 * i);
    }
    v |= static_cast<std::uint64_t>(sega_crc(stat, crc_start)) << 36;
    if (is_data && carries_is_data)
        v |= (std::uint64_t{1} << 40);
    return v;
}

std::array<std::uint16_t, 3> sega_set_words(std::uint64_t frame) noexcept {

    return {
        lo16(frame),
        lo16(frame >> 16),
        static_cast<std::uint16_t>(((frame >> 32) & 0xFFu) | (((frame >> 40) & 1u) << 8))
    };
}

std::optional<std::uint16_t> sega_send_data_query(TrackType type, bool skip_mode1) noexcept {

    if (skip_mode1 && type == TrackType::Mode1)
        return std::nullopt;
    return static_cast<std::uint16_t>(
        kCddGetSendData | (static_cast<std::uint16_t>(static_cast<std::uint8_t>(type)) << 2)
    );
}

std::array<std::uint16_t, 2> pce_set_words(
    std::uint16_t status,
    bool region,
    bool data_request
) noexcept {

    return {status, static_cast<std::uint16_t>((region ? 2u : 0u) | (data_request ? 1u : 0u))};
}

std::array<std::uint8_t, 96> interleave_subcode(std::span<const std::uint8_t, 96> raw) noexcept {

    std::array<std::uint8_t, 96> out{};
    for (std::size_t i = 0; i < 96; ++i) {
        unsigned code = 0;
        for (std::size_t j = 0; j < 8; ++j) {
            const unsigned bit = (static_cast<unsigned>(raw[j * 12 + (i >> 3)]) >> (7u - (i & 7u)))
                & 1u;
            code |= bit << (7u - j);
        }
        out[i] = static_cast<std::uint8_t>(code);
    }
    return out;
}

}

}
