// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/reg_field.h"

namespace mister::hal::ocram {

class RecordHeader;

class InfoArg {
public:
    constexpr InfoArg() noexcept = default;

    [[nodiscard]] static constexpr InfoArg of(std::uint8_t info) noexcept { return InfoArg{info}; }

    [[nodiscard]] constexpr std::uint8_t byte() const noexcept { return v; }

    friend constexpr bool operator==(InfoArg, InfoArg) = default;

private:
    friend class RecordHeader;

    constexpr explicit InfoArg(std::uint8_t info) noexcept : v(info) {}

    [[nodiscard]] constexpr RegField<std::uint32_t>::Value field() const noexcept {
        return RegField<std::uint32_t>::Value{v};
    }

    [[nodiscard]] static constexpr InfoArg of_field(std::uint32_t field) noexcept {
        return InfoArg{static_cast<std::uint8_t>(field)};
    }

    std::uint8_t v = 0;
};

}
