// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "svc/filter_bank_index.h"

namespace mister::svc {

enum class FilterWireBank : std::uint8_t {
    Horiz = 0,
    Vert = 1,
    AdaptiveHoriz = 2,
    AdaptiveVert = 3,
};

[[nodiscard]] constexpr bool is_vert(FilterWireBank bank) noexcept {
    return bank == FilterWireBank::Vert || bank == FilterWireBank::AdaptiveVert;
}

[[nodiscard]] constexpr bool is_adaptive(FilterWireBank bank) noexcept {
    return bank == FilterWireBank::AdaptiveHoriz || bank == FilterWireBank::AdaptiveVert;
}

inline constexpr unsigned kFilterAddrShift = 9;

[[nodiscard]] constexpr FilterBankIndex base_index(FilterWireBank bank) noexcept {
    return FilterBankIndex::of(bank);
}

struct FilterSendPlan {
    struct Seg {
        FilterWireBank bank = FilterWireBank::Horiz;
    };

    struct SegIndex {
        std::uint8_t v = 0;

        [[nodiscard]] constexpr SegIndex next() const noexcept {
            const std::uint8_t place = static_cast<std::uint8_t>(v + 1);
            return SegIndex{place};
        }

        friend constexpr bool operator==(SegIndex, SegIndex) = default;
    };

    std::array<Seg, 3> segs{};
    std::uint8_t count = 0;

    [[nodiscard]] const Seg& seg(SegIndex i) const noexcept { return segs[i.v]; }

    [[nodiscard]] bool covers(SegIndex i) const noexcept { return i.v < count; }
};

}
