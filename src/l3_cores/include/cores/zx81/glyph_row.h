// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mister::cores::zx81 {

struct GlyphRow {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr std::optional<GlyphRow> of_text(std::string_view text) noexcept {
        constexpr std::size_t kPixels = 8;
        if (text.size() < kPixels)
            return std::nullopt;
        unsigned bits = 0;
        for (const char c : text.substr(0, kPixels))
            bits = (bits << 1) | (c == '1' ? 1u : 0u);
        return GlyphRow{static_cast<std::uint8_t>(bits)};
    }

    [[nodiscard]] constexpr GlyphRow inverted() const noexcept {
        return GlyphRow{static_cast<std::uint8_t>(~static_cast<unsigned>(v))};
    }

    friend constexpr bool operator==(GlyphRow, GlyphRow) = default;
};

static_assert(
    GlyphRow::of_text("10000001") == GlyphRow{0x81}
        && !GlyphRow::of_text("1111111")
        && GlyphRow::of_text(" 1111111x") == GlyphRow{0x7F}
        && GlyphRow{0x0F}.inverted() == GlyphRow{0xF0},
    "user_io.cpp's chr_parse: eight characters from the text's first byte"
);

}
