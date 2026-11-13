// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/seat.h"

namespace mister::svc {

class ViewGen {
    TASTY_SEAT_EXEMPT(component);

public:
    constexpr ViewGen() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t value() const noexcept { return value_; }

    [[nodiscard]] constexpr ViewGen next() const noexcept { return ViewGen{value_ + 1}; }

    friend constexpr bool operator==(const ViewGen&, const ViewGen&) = default;

private:
    constexpr explicit ViewGen(std::uint32_t value) noexcept : value_(value) {}

    std::uint32_t value_ = 0;
};

static_assert(std::is_trivially_copyable_v<ViewGen>);
static_assert(std::has_unique_object_representations_v<ViewGen>);
static_assert(sizeof(ViewGen) == sizeof(std::uint32_t));

}
