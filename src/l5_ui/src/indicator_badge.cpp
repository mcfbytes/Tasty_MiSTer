// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/indicator_badge.h"

#include <algorithm>
#include <tuple>

#include "infra/message_sum.h"
#include "proto/info_window.h"
#include "ui/glyph_rom.h"

namespace mister::ui {
namespace {

constexpr IconArt kBlankArt{};

constexpr IconArt kCardArt{
    "..#########.....",
    "..##########....",
    "..#.#.#.#####...",
    "..#.#.#.######..",
    "..#.#.#.######..",
    "..############..",
    "..############..",
    "..############..",
    "..############..",
    "..############..",
    "..############..",
    "..############..",
    "..############..",
    "..############..",
    "..############..",
    "................",
};

constexpr IconArt kCardFaultArt{
    "..#########.....",
    "..##########....",
    "..#.#.#.#####...",
    "..#.#.#.######..",
    "..#.#.#.######..",
    "..############..",
    "..############..",
    "..###.#####.##..",
    "..####.###.###..",
    "..#####.#.####..",
    "..######.#####..",
    "..#####.#.####..",
    "..####.###.###..",
    "..###.#####.##..",
    "..############..",
    "................",
};

constexpr std::array<const IconArt*, infra::ordinal(IndicatorIcon::kCount)> kIcons{
    &kBlankArt,
    &kCardArt,
    &kCardFaultArt
};

}

const IconArt& icon_art(IndicatorIcon icon) noexcept {
    return *kIcons[infra::ordinal(icon) < kIcons.size() ? infra::ordinal(icon) : 0];
}

IndicatorBadge::IndicatorBadge(std::span<const IndicatorCanvas> lines) noexcept {

    constexpr std::size_t kPad = 4;
    constexpr std::size_t kIconGap = 2;
    constexpr std::size_t kBand = 16;

    constexpr std::size_t kRowPixels = 8;
    constexpr std::size_t kTextDrop = (kBand - kGlyphBytes) / 2;
    static_assert(
        (kPad + kTextDrop) % kRowPixels == 0 && kBand % kRowPixels == 0,
        "a line's text lands on a whole OSD row"
    );

    constexpr std::array<std::uint8_t, kRowPixels>
        kPixelBit{0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80};
    constexpr std::uint8_t kAllLit = 0xFF;
    constexpr std::size_t kIconSide = std::tuple_size_v<IconArt>;

    const std::span<const IndicatorCanvas> drawn = lines.first(
        std::min(lines.size(), std::tuple_size_v<Lines>)
    );
    if (drawn.empty())
        return;
    const bool icons = std::ranges::any_of(drawn, [](const IndicatorCanvas& line) {
        return line.icon() != IndicatorIcon::None;
    });
    const std::size_t text_x = kPad + (icons ? kIconSide + kIconGap : 0);
    std::size_t widest = 0;
    std::size_t band_top = kPad;
    for (const IndicatorCanvas& line : drawn) {
        const IconArt& art = icon_art(line.icon());
        for (std::size_t y = 0; y < art.size(); ++y) {
            const std::size_t py = band_top + y;
            for (std::size_t x = 0; x < art[y].size() && x < kIconSide; ++x) {
                if (art[y][x] == '#')
                    rows_[py / kRowPixels][kPad + x] |= kPixelBit[py % kRowPixels];
            }
        }
        RowBytes& text_row = rows_[(band_top + kTextDrop) / kRowPixels];
        std::size_t x = text_x;
        proto::OsdCol cell{};
        for (const char ch : line.text()) {
            if (x + kGlyphBytes > text_row.size())
                break;
            const bool inverted = line.inverts(cell);
            for (std::size_t b = 0; b < kGlyphBytes; ++b) {
                std::uint8_t column = kGlyphRom[static_cast<unsigned char>(ch)][b];
                if (inverted)
                    column ^= kAllLit;
                text_row[x + b] |= column;
            }
            x += kGlyphBytes;
            cell += proto::OsdColCount{1};
        }
        widest = std::max(widest, x);
        band_top += kBand;
    }
    width_ = std::min(
        proto::OsdColCount::of((widest + kPad + kRowPixels - 1) / kRowPixels),
        proto::kInfoMaxWidth
    );
    height_ = proto::OsdRowCount::of((band_top + kPad) / kRowPixels);
}

IndicatorBadge::RowBytes IndicatorBadge::row(proto::OsdRow row) const noexcept {
    if (!height_.covers(row))
        return RowBytes{};
    return proto::row_cell(rows_, row);
}

}
