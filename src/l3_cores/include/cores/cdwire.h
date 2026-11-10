// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

#include "hal/types.h"
#include "proto/types.h"
#include "proto/uio_opcodes.h"
#include "svc/msf.h"
#include "svc/track.h"
#include "svc/types.h"

namespace mister::cores::cdwire {

inline constexpr std::uint8_t kUioCdGet = static_cast<std::uint8_t>(proto::UioOp::CdGet);
inline constexpr std::uint8_t kUioCdSet = static_cast<std::uint8_t>(proto::UioOp::CdSet);

struct Selector {
    std::uint16_t v = 0;

    [[nodiscard]] static constexpr Selector send_data_query(svc::TrackType type) noexcept;

    [[nodiscard]] constexpr hal::SpiWord word() const noexcept { return hal::SpiWord{v}; }

    friend constexpr bool operator==(Selector, Selector) = default;
};

inline constexpr Selector kCddGetCmd{0};
inline constexpr Selector kCddGetSendData{1};

constexpr Selector Selector::send_data_query(svc::TrackType type) noexcept {
    constexpr unsigned kTypeShift = 2;
    const auto type_bits = static_cast<std::uint16_t>(type);
    return Selector{static_cast<std::uint16_t>(
        kCddGetSendData.v | static_cast<std::uint16_t>(type_bits << kTypeShift)
    )};
}

using Msf = svc::Msf;

constexpr std::uint8_t bcd(std::uint32_t v) noexcept {
    return proto::PackedBcd::unmasked_byte(v);
}

constexpr std::uint8_t bcd(svc::TrackNumber n) noexcept {
    return n.bcd_byte();
}

std::uint64_t sega_compose(std::uint16_t w0, std::uint16_t w1, std::uint16_t w2) noexcept;

std::uint8_t sega_crc(std::span<const std::uint8_t, 9> nibbles, std::uint8_t crc_start) noexcept;

bool sega_command_ok(std::uint64_t c, std::uint8_t crc_start) noexcept;

std::uint64_t sega_pack_status(
    std::span<const std::uint8_t, 9> stat,
    bool is_data,
    std::uint8_t crc_start,
    bool carries_is_data
) noexcept;

std::array<std::uint16_t, 3> sega_set_words(std::uint64_t frame) noexcept;

std::optional<Selector> sega_send_data_query(svc::TrackType type, bool skip_mode1) noexcept;

std::array<std::uint16_t, 2> pce_set_words(
    std::uint16_t status,
    bool region,
    bool data_request
) noexcept;

}
