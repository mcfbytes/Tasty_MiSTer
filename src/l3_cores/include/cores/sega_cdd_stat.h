// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/cdwire.h"
#include "infra/seat.h"
#include "svc/types.h"

namespace mister::cores {

struct SegaCddStat {
    TASTY_SEAT_EXEMPT(component);
    static constexpr std::size_t kBytes = 9;
    static constexpr std::uint8_t kNibbleMask = 0x0F;
    static constexpr unsigned kNibbleBits = 4;

    static constexpr std::uint8_t kAbsent = 0x0A;

    enum class Field : std::uint8_t {
        Status = 0,
        Subcommand = 1,
        Bcd0Tens = 2,
        Bcd0Ones = 3,
        Bcd1Tens = 4,
        Bcd1Ones = 5,
        Bcd2Tens = 6,
        Bcd2Ones = 7,
        Type = 8,
        kCount = 9,
    };

    enum class BcdSlot : std::uint8_t { Bcd0, Bcd1, Bcd2 };

    std::array<std::uint8_t, kBytes> bytes{};

    [[nodiscard]] constexpr std::uint8_t at(Field f) const noexcept {
        return bytes[static_cast<std::size_t>(f)];
    }

    constexpr void set(Field f, std::uint8_t v) noexcept { bytes[static_cast<std::size_t>(f)] = v; }

    [[nodiscard]] constexpr std::uint8_t status() const noexcept { return at(Field::Status); }

    constexpr void set_status(std::uint8_t v) noexcept { set(Field::Status, v); }

    [[nodiscard]] constexpr std::uint8_t subcommand() const noexcept {
        return at(Field::Subcommand);
    }

    constexpr void set_subcommand(std::uint8_t v) noexcept { set(Field::Subcommand, v); }

    [[nodiscard]] constexpr std::uint8_t type_byte() const noexcept { return at(Field::Type); }

    constexpr void set_type_byte(std::uint8_t v) noexcept { set(Field::Type, v); }

    constexpr void set_bcd(BcdSlot slot, std::uint32_t value) noexcept {
        set_digits_(slot, cdwire::bcd(value));
    }

    constexpr void set_bcd(BcdSlot slot, svc::TrackNumber n) noexcept {
        set_digits_(slot, n.bcd_byte());
    }

    constexpr void set_bcd(BcdSlot slot, svc::TrackCount n) noexcept {
        set_digits_(slot, n.bcd_byte());
    }

    constexpr void set_msf(cdwire::Msf msf) noexcept {
        set_bcd(BcdSlot::Bcd0, msf.m);
        set_bcd(BcdSlot::Bcd1, msf.s);
        set_bcd(BcdSlot::Bcd2, msf.f);
    }

    constexpr void clear_from(Field from) noexcept {
        for (std::size_t i = static_cast<std::size_t>(from); i < kBytes; ++i)
            bytes[i] = 0;
    }

    [[nodiscard]] constexpr std::span<const std::uint8_t, kBytes> nibbles() const noexcept {
        return bytes;
    }

    [[nodiscard]] std::uint8_t checksum(std::uint8_t crc_start) const noexcept {
        return cdwire::sega_crc(nibbles(), crc_start);
    }

    [[nodiscard]] std::uint64_t packed(
        bool is_data,
        std::uint8_t crc_start,
        bool carries_is_data
    ) const noexcept {
        return cdwire::sega_pack_status(nibbles(), is_data, crc_start, carries_is_data);
    }

    [[nodiscard]] std::array<std::uint16_t, 3> wire_words(
        bool is_data,
        std::uint8_t crc_start,
        bool carries_is_data
    ) const noexcept {
        return cdwire::sega_set_words(packed(is_data, crc_start, carries_is_data));
    }

    friend constexpr bool operator==(SegaCddStat, SegaCddStat) = default;

private:
    constexpr void set_digits_(BcdSlot slot, std::uint8_t packed) noexcept {
        const auto tens = static_cast<std::size_t>(Field::Bcd0Tens)
            + 2 * static_cast<std::size_t>(slot);
        bytes[tens] = static_cast<std::uint8_t>(packed >> kNibbleBits);
        bytes[tens + 1] = static_cast<std::uint8_t>(packed & kNibbleMask);
    }
};

static_assert(sizeof(SegaCddStat) == 9);

}
