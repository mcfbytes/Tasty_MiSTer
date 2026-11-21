// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

namespace mister::svc::connect {

struct ViewGen {
    std::uint32_t v = 0;

    [[nodiscard]] constexpr ViewGen next() const noexcept { return ViewGen{v + 1}; }

    friend constexpr bool operator==(ViewGen, ViewGen) = default;
};

static_assert(std::is_trivially_copyable_v<ViewGen>);
static_assert(std::has_unique_object_representations_v<ViewGen>);
static_assert(sizeof(ViewGen) == sizeof(std::uint32_t));

}
