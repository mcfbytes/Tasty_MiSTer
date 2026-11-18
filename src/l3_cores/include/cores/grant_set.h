// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <initializer_list>

namespace mister::cores {

enum class Grant : std::uint8_t {
    Disk = 1u << 0,
    Mailbox = 1u << 1,
    SaveCopy = 1u << 2,
    Lamp = 1u << 3,
    Discs = 1u << 4,
};

class GrantSet {
public:
    constexpr GrantSet() = default;

    constexpr GrantSet(std::initializer_list<Grant> grants) noexcept {
        for (const Grant g : grants)
            v = static_cast<std::uint8_t>(v | bit(g));
    }

    [[nodiscard]] constexpr bool has(Grant g) const noexcept { return (v & bit(g)) != 0; }

    [[nodiscard]] constexpr bool none() const noexcept { return v == 0; }

    friend constexpr bool operator==(GrantSet, GrantSet) = default;

private:
    [[nodiscard]] static constexpr std::uint8_t bit(Grant g) noexcept {
        return static_cast<std::uint8_t>(g);
    }

    std::uint8_t v = 0;
};

}
