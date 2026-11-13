// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

#include "infra/seat.h"

namespace mister::svc {

class PromptGen {
    TASTY_SEAT_EXEMPT(component);

public:
    constexpr PromptGen() noexcept = default;

    [[nodiscard]] constexpr std::uint32_t value() const noexcept { return value_; }

    [[nodiscard]] constexpr PromptGen next() const noexcept { return PromptGen{value_ + 1}; }

    friend constexpr bool operator==(const PromptGen&, const PromptGen&) = default;

private:
    constexpr explicit PromptGen(std::uint32_t value) noexcept : value_(value) {}

    std::uint32_t value_ = 0;
};

static_assert(std::is_trivially_copyable_v<PromptGen>);
static_assert(std::has_unique_object_representations_v<PromptGen>);
static_assert(sizeof(PromptGen) == sizeof(std::uint32_t));

}
