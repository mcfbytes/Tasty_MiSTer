// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#include "hal/reg_field.h"

namespace mister::hal::ocram {

class RecordHeader;

class RumbleArg {
public:
    static constexpr std::size_t kPads = 6;

    constexpr RumbleArg() noexcept = default;

    [[nodiscard]] static constexpr std::optional<RumbleArg> of(std::size_t pad) noexcept {
        if (pad >= kPads)
            return std::nullopt;
        return RumbleArg{static_cast<std::uint8_t>(pad)};
    }

    [[nodiscard]] constexpr std::size_t pad() const noexcept { return v; }

    friend constexpr bool operator==(RumbleArg, RumbleArg) = default;

private:
    friend class RecordHeader;

    constexpr explicit RumbleArg(std::uint8_t pad) noexcept : v(pad) {}

    [[nodiscard]] constexpr RegField<std::uint32_t>::Value field() const noexcept {
        return RegField<std::uint32_t>::Value{v};
    }

    [[nodiscard]] static constexpr std::optional<RumbleArg> of_field(std::uint32_t field) noexcept {
        return of(field);
    }

    std::uint8_t v = 0;
};

static_assert(RumbleArg::of(5).has_value() && !RumbleArg::of(6).has_value());

}
