// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "svc/disc_engine.h"

namespace mister::cores::cdwire {

inline constexpr std::uint8_t kUioCdGet = 0x34;
inline constexpr std::uint8_t kUioCdSet = 0x35;

inline constexpr std::uint16_t kCddGetCmd = 0;
inline constexpr std::uint16_t kCddGetSendData = 1;

struct Msf {
    std::uint32_t m = 0, s = 0, f = 0;
};

constexpr Msf lba_to_msf(std::int32_t lba) noexcept {
    const std::uint32_t v = lba > 0 ? static_cast<std::uint32_t>(lba) : 0u;
    return Msf{(v / 75u) / 60u, (v / 75u) % 60u, v % 75u};
}

constexpr std::uint8_t bcd(std::uint32_t v) noexcept {
    return static_cast<std::uint8_t>(((v / 10u) << 4) | (v % 10u));
}

std::uint64_t sega_compose(std::uint16_t w0, std::uint16_t w1, std::uint16_t w2) noexcept;

std::uint8_t sega_crc(std::span<const std::uint8_t, 9> nibbles, std::uint8_t crc_start) noexcept;

bool sega_command_ok(std::uint64_t c, std::uint8_t crc_start) noexcept;

std::uint64_t sega_pack_status(std::span<const std::uint8_t, 9> stat, bool is_data,
                               std::uint8_t crc_start, bool carries_is_data) noexcept;

std::array<std::uint16_t, 3> sega_set_words(std::uint64_t frame) noexcept;

std::optional<std::uint16_t> sega_send_data_query(svc::TrackType type, bool skip_mode1) noexcept;

std::array<std::uint16_t, 2> pce_set_words(std::uint16_t status, bool region,
                                           bool data_request) noexcept;

std::array<std::uint8_t, 96> interleave_subcode(std::span<const std::uint8_t, 96> raw) noexcept;

}  // namespace mister::cores::cdwire
