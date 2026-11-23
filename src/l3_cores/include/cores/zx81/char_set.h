// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "cores/zx81/glyph_row.h"
#include "cores/zx81/glyph_slot.h"
#include "cores/zx81/pixel_line.h"
#include "infra/byte_view.h"
#include "infra/indexed_array.h"

namespace mister::cores::zx81 {

class CharSet {
public:
    using Glyph = infra::IndexedArray<GlyphRow, PixelLine::count(), PixelLine>;

    [[nodiscard]] static CharSet stock() noexcept;

    constexpr void set(GlyphSlot slot, PixelLine line, GlyphRow row) noexcept {
        infra::at_index(glyphs_, slot)[line] = row;
    }

    [[nodiscard]] constexpr GlyphRow row(GlyphSlot slot, PixelLine line) const noexcept {
        return infra::at_index(glyphs_, slot)[line];
    }

    [[nodiscard]] std::vector<std::uint8_t> image() const {
        const auto bytes = infra::byte_cast<const std::uint8_t>(infra::bytes_of(glyphs_));
        return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
    }

private:
    std::array<Glyph, GlyphSlot::count()> glyphs_{};
};

static_assert(
    sizeof(CharSet) == GlyphSlot::count() * PixelLine::count(),
    "the set is its glyph bytes and nothing else"
);

}
