// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>

#include "infra/decimal_text.h"
#include "infra/fixed_str.h"
#include "infra/json_out.h"

namespace mister::app {

using SegmentText = FixedStr<8, StrFit::Reject>;

struct SegmentIndex {
    std::uint16_t v{};

    [[nodiscard]] static constexpr SegmentIndex last() noexcept {
        return SegmentIndex{std::numeric_limits<std::uint16_t>::max()};
    }

    [[nodiscard]] constexpr SegmentIndex next() const noexcept {
        return SegmentIndex{static_cast<std::uint16_t>(v + 1u)};
    }

    [[nodiscard]] constexpr infra::DecimalText digits() const noexcept {
        return infra::DecimalText{v};
    }

    friend constexpr void json_field(
        infra::JsonOut& o,
        infra::JsonName key,
        SegmentIndex segment
    ) noexcept {
        o.field(key, segment.v);
    }

    friend constexpr bool operator==(SegmentIndex, SegmentIndex) = default;

    [[nodiscard]] SegmentText text() const noexcept {
        char buf[SegmentText::kBufSize]{};
        const int n = std::snprintf(buf, sizeof buf, "_%03u", static_cast<unsigned>(v));
        SegmentText out{};
        if (n > 0 && static_cast<std::size_t>(n) < sizeof buf)
            (void)out.assign(std::string_view{buf, static_cast<std::size_t>(n)});
        return out;
    }
};

}
