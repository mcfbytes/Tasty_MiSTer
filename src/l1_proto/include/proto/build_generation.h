// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

namespace mister::proto {

class BuildGeneration {
public:
    constexpr BuildGeneration() noexcept = default;

    [[nodiscard]] static constexpr BuildGeneration of_wire(std::uint16_t w) noexcept {
        return BuildGeneration{w};
    }

    [[nodiscard]] constexpr std::uint16_t wire() const noexcept { return v; }

    [[nodiscard]] constexpr BuildGeneration next() const noexcept {
        return v == 0xFFFF ? BuildGeneration{1}
                           : BuildGeneration{static_cast<std::uint16_t>(v + 1)};
    }

    [[nodiscard]] constexpr bool built() const noexcept { return v != 0; }

    friend constexpr bool operator==(BuildGeneration, BuildGeneration) = default;

private:
    constexpr explicit BuildGeneration(std::uint16_t w) noexcept : v(w) {}

    std::uint16_t v = 0;
};

static_assert(sizeof(BuildGeneration) == 2 && std::is_trivially_copyable_v<BuildGeneration>);
static_assert(BuildGeneration::of_wire(0xFFFF).next() == BuildGeneration::of_wire(1));

}
