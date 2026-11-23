// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "cores/stock_scan.h"

namespace mister::cores::zx81 {

struct GlyphSlot {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr std::size_t count() noexcept {
        constexpr std::size_t kGlyphs = 128;
        return kGlyphs;
    }

    [[nodiscard]] static constexpr std::optional<GlyphSlot> of_text(
        std::string_view decimal
    ) noexcept {
        constexpr int kNormalLast = 63;
        constexpr int kInverseFirst = 128;
        constexpr int kInverseLast = 191;
        constexpr int kInverseDrop = 64;
        const int code = stock_scan::scan_atoi(decimal);
        if (code >= 0 && code <= kNormalLast)
            return GlyphSlot{static_cast<std::uint8_t>(code)};
        if (code >= kInverseFirst && code <= kInverseLast)
            return GlyphSlot{static_cast<std::uint8_t>(code - kInverseDrop)};
        return std::nullopt;
    }

    [[nodiscard]] constexpr bool inverse() const noexcept {
        constexpr std::uint8_t kInverseFrom = 64;
        return v >= kInverseFrom;
    }

    friend constexpr bool operator==(GlyphSlot, GlyphSlot) = default;
};

static_assert(
    GlyphSlot::of_text("63") == GlyphSlot{63}
    && !GlyphSlot::of_text("64")
    && GlyphSlot::of_text("128") == GlyphSlot{64}
    && GlyphSlot::of_text("191") == GlyphSlot{127}
    && !GlyphSlot::of_text("192")
    && GlyphSlot{64}.inverse()
    && !GlyphSlot{63}.inverse()
);

}
