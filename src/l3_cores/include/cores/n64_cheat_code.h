// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/cheat_geometry.h"
#include "infra/seat.h"

namespace mister::cores::n64 {

enum class Compare : std::uint8_t {
    Always = 0x0,
    Equal = 0x1,
    Greater = 0x2,
    Less = 0x3,
    GreaterEq = 0x4,
    LessEq = 0x5,
    NotEq = 0x6,
    Empty = 0xf,
};

[[nodiscard]] constexpr std::optional<Compare> compare_from_raw(std::uint8_t raw) noexcept {
    switch (raw) {
        case 0x0:
            return Compare::Always;
        case 0x1:
            return Compare::Equal;
        case 0x2:
            return Compare::Greater;
        case 0x3:
            return Compare::Less;
        case 0x4:
            return Compare::GreaterEq;
        case 0x5:
            return Compare::LessEq;
        case 0x6:
            return Compare::NotEq;
        case 0xf:
            return Compare::Empty;
        default:
            return std::nullopt;
    }
}

struct CheatCode {

    TASTY_SEAT_EXEMPT(component);

    static constexpr std::size_t kBytes = 16;

    static constexpr std::size_t kMax = kCheatTableBytes / kBytes;

    std::uint32_t address = 0;
    std::uint32_t compare = 0;
    std::uint32_t replace = 0;
    std::optional<Compare> cmp_type = Compare::Always;
    std::uint8_t cmp_mask = 0;
    bool boot_code = false;
    bool boot_code_done = false;
    bool gs_button = false;

    [[nodiscard]] static constexpr CheatCode decode(
        std::span<const std::uint8_t, kBytes> rec
    ) noexcept {
        const auto word = [rec](std::size_t at) {
            return static_cast<std::uint32_t>(rec[at])
                | (static_cast<std::uint32_t>(rec[at + 1]) << 8)
                | (static_cast<std::uint32_t>(rec[at + 2]) << 16)
                | (static_cast<std::uint32_t>(rec[at + 3]) << 24);
        };
        CheatCode c;
        c.cmp_type = compare_from_raw(static_cast<std::uint8_t>(rec[0] & 0x0fu));
        c.cmp_mask = static_cast<std::uint8_t>((rec[0] >> 4) & 0x0fu);
        c.boot_code = (rec[1] & 0x01u) != 0u;
        c.boot_code_done = (rec[1] & 0x02u) != 0u;
        c.gs_button = (rec[1] & 0x04u) != 0u;
        c.address = word(4);
        c.compare = word(8);
        c.replace = word(12);
        return c;
    }

    [[nodiscard]] constexpr std::size_t span_bytes() const noexcept {
        std::size_t n = 0;
        for (std::size_t i = 0; (cmp_mask >> i) != 0; ++i)
            n = i + 1;
        return n;
    }

    [[nodiscard]] constexpr bool chained() const noexcept { return (compare & 0x1u) != 0u; }
};

}
