// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace mister::cores {

struct RamImageRecipe {

    enum class Kind : std::uint8_t { None, Flat, Pcg32, Lfsr };

    enum class Entropy : std::uint8_t { None, Low, High };
    Kind kind = Kind::None;
    Entropy entropy = Entropy::Low;
    std::uint8_t first_fill = 0;
    std::uint8_t second_fill = 0;
    std::uint32_t seed = 0;
    std::uint32_t first_bytes = 0;
    std::uint32_t second_bytes = 0;

    [[nodiscard]] constexpr std::uint32_t size() const noexcept {
        return first_bytes + second_bytes;
    }
};
static_assert(std::is_trivially_copyable_v<RamImageRecipe>);

[[nodiscard]] std::vector<std::uint8_t> expand(const RamImageRecipe& r);

}  // namespace mister::cores
