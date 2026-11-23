// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace mister::proto {

struct MailboxSelectCount {
    std::uint8_t v = 0;
    using affine_count = MailboxSelectCount;

    [[nodiscard]] constexpr std::size_t size() const noexcept { return v; }

    template <class Row>
    [[nodiscard]] constexpr bool bounds(std::span<const Row> table) const noexcept {
        return table.size() <= v;
    }

    friend constexpr bool operator==(MailboxSelectCount, MailboxSelectCount) = default;
    friend constexpr auto operator<=>(MailboxSelectCount, MailboxSelectCount) = default;
};

}
