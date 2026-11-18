// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "hal/sys_block.h"

namespace mister::proto {

class StatusChange {
public:
    constexpr StatusChange() noexcept = default;

    [[nodiscard]] static constexpr std::optional<StatusChange> of_byte(std::uint8_t wire) noexcept {
        const std::optional<hal::SysBlock::Stflg> stflg = hal::SysBlock::decode(wire);
        if (!stflg)
            return std::nullopt;
        return StatusChange{stflg->flag};
    }

    [[nodiscard]] static constexpr StatusChange of(const hal::SysBlock::Stflg& stflg) noexcept {
        return StatusChange{stflg.flag};
    }

    [[nodiscard]] constexpr std::uint8_t nibble() const noexcept { return nibble_; }

    friend constexpr bool operator==(StatusChange, StatusChange) = default;

private:
    explicit constexpr StatusChange(std::uint8_t nibble) noexcept : nibble_(nibble) {}

    std::uint8_t nibble_ = 0;
};

static_assert(
    StatusChange::of_byte(0xA3) == StatusChange::of(hal::SysBlock::Stflg{0, 3}),
    "the 0x29 head byte and the decoded STFLG name the same change"
);
static_assert(!StatusChange::of_byte(0x13), "the 0xA mark is L0's");
static_assert(StatusChange{} == *StatusChange::of_byte(0xA0), "the zero nibble is a valid change");
static_assert(sizeof(StatusChange) == 1);

}
