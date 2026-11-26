// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/reg_field.h"

namespace mister::hal::ocram {

class RecordHeader;

class UploadArg {
public:
    constexpr UploadArg() noexcept = default;

    [[nodiscard]] static constexpr UploadArg of(std::uint8_t index) noexcept {
        return UploadArg{index};
    }

    [[nodiscard]] constexpr std::uint8_t index() const noexcept { return v; }

    friend constexpr bool operator==(UploadArg, UploadArg) = default;

private:
    friend class RecordHeader;

    constexpr explicit UploadArg(std::uint8_t index) noexcept : v(index) {}

    [[nodiscard]] constexpr RegField<std::uint32_t>::Value field() const noexcept {
        return RegField<std::uint32_t>::Value{v};
    }

    [[nodiscard]] static constexpr UploadArg of_field(std::uint32_t field) noexcept {
        return UploadArg{static_cast<std::uint8_t>(field)};
    }

    std::uint8_t v = 0;
};

}
