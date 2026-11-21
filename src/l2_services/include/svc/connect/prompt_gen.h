// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

namespace mister::svc::connect {

struct PromptGen {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr PromptGen next() const noexcept { return PromptGen{v + 1}; }

    friend constexpr bool operator==(PromptGen, PromptGen) = default;
};

static_assert(std::is_trivially_copyable_v<PromptGen>);
static_assert(std::has_unique_object_representations_v<PromptGen>);
static_assert(sizeof(PromptGen) == sizeof(std::uint32_t));

}
