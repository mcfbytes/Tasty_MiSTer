// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <type_traits>

namespace mister::xthread {

class GrantGen {
public:
    constexpr GrantGen() noexcept = default;

    template <class Reader>
    [[nodiscard]] static constexpr GrantGen seen_by(const Reader& r) noexcept {
        return GrantGen{r.seen()};
    }

    template <class Cell>
    [[nodiscard]] bool is_current_in(const Cell& cell) const noexcept {
        return cell.generation() == v;
    }

    [[nodiscard]] constexpr bool held() const noexcept { return v != 0; }

    friend constexpr bool operator==(GrantGen, GrantGen) = default;

private:
    constexpr explicit GrantGen(std::uint32_t telemetry_generation) noexcept
        : v(telemetry_generation) {}

    std::uint32_t v = 0;
};

static_assert(std::is_trivially_copyable_v<GrantGen>);

}
