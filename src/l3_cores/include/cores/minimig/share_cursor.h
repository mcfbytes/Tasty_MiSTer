// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace mister::cores::minimig {

struct ShareCursor {
    static constexpr std::int32_t kFirst = 666;
    std::int32_t v = kFirst;

    [[nodiscard]] constexpr ShareCursor next() const noexcept { return ShareCursor{v + 1}; }

    [[nodiscard]] const std::string* in(std::span<const std::string> listing) const noexcept {
        const std::uint32_t at = static_cast<std::uint32_t>(v) - static_cast<std::uint32_t>(kFirst);
        return at < listing.size() ? &listing[at] : nullptr;
    }

    friend constexpr bool operator==(ShareCursor, ShareCursor) = default;
};

}
