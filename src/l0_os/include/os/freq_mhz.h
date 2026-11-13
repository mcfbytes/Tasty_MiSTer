// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/decimal_text.h"
#include "infra/seat.h"

namespace mister::os {

class FreqMhz {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] static constexpr FreqMhz of(std::uint16_t mhz) noexcept { return FreqMhz{mhz}; }

    [[nodiscard]] constexpr std::uint16_t mhz() const noexcept { return mhz_; }

    [[nodiscard]] infra::DecimalText text() const noexcept { return infra::DecimalText{mhz_}; }

    friend constexpr bool operator==(const FreqMhz&, const FreqMhz&) = default;

private:
    constexpr FreqMhz() noexcept = default;

    constexpr explicit FreqMhz(std::uint16_t mhz) noexcept : mhz_(mhz) {}

    std::uint16_t mhz_ = 0;
};

static_assert(std::is_trivially_copyable_v<FreqMhz>);
static_assert(std::has_unique_object_representations_v<FreqMhz>);
static_assert(sizeof(FreqMhz) == sizeof(std::uint16_t));

}
