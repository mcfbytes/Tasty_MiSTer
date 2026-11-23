// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "cores/zx81/code_count.h"
#include "cores/zx81/colour_attr.h"
#include "cores/zx81/colour_code.h"
#include "cores/zx81/pixel_line.h"
#include "infra/byte_view.h"
#include "infra/indexed_array.h"
#include "infra/span_cut.h"

namespace mister::cores::zx81 {

class ColourMap {
public:
    using Cell = infra::IndexedArray<ColourAttr, PixelLine::count(), PixelLine>;

    constexpr void paint(ColourCode first, CodeCount count, const Cell& cell) noexcept {
        const std::span<Cell> run = infra::affine::first_clamped(
            infra::affine::from(std::span<Cell>(cells_), first),
            count
        );
        std::ranges::fill(run, cell);
    }

    [[nodiscard]] constexpr ColourAttr border() const noexcept { return border_; }

    constexpr void set_border(ColourAttr a) noexcept { border_ = a; }

    [[nodiscard]] constexpr const Cell& cell(ColourCode code) const noexcept {
        return infra::at_index(cells_, code);
    }

    [[nodiscard]] std::vector<std::uint8_t> image() const {
        std::vector<std::uint8_t> out;
        const auto table = infra::byte_cast<const std::uint8_t>(infra::bytes_of(cells_));
        out.assign(table.begin(), table.end());
        const auto last = infra::byte_cast<const std::uint8_t>(infra::bytes_of(border_));
        out.insert(out.end(), last.begin(), last.end());
        return out;
    }

private:
    std::array<Cell, ColourCode::count()> cells_{};
    ColourAttr border_{};
};

static_assert(
    sizeof(ColourMap::Cell) == PixelLine::count(),
    "a cell is its eight attribute bytes and nothing else"
);

}
