// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "proto/osd_surface.h"
#include "proto/types.h"
#include "ui/types.h"
#include "infra/fixed_str.h"
#include "infra/seat.h"

namespace mister::ui {

struct StatusBarFacts;

using proto::OsdRow;

enum class Glyph : std::uint8_t {
    SubmenuArrow = 0x16,
    CheatOn = 0x1A,
    CheatOff = 0x1B,
    ArrowUp = 0x11,
    ArrowDown = 0x10,
    BarFilled = 0x7F,
    BarEmpty = 0x8C,
};

[[nodiscard]] constexpr unsigned char glyph_code(Glyph g) noexcept {
    return static_cast<unsigned char>(g);
}

[[nodiscard]] constexpr char glyph_char(Glyph g) noexcept {
    return static_cast<char>(g);
}

inline constexpr unsigned kOsdColumns = 32;

inline constexpr unsigned kSideStripeBytes = 22;

inline constexpr unsigned kProgressMax = 167;

inline constexpr unsigned kClassicRowColumns = 28;

inline constexpr unsigned kArrowColumn = kClassicRowColumns - 1;

inline constexpr unsigned kDirectoryRowColumns = kOsdColumns;

inline constexpr std::size_t kTitleBytes = 256;

inline constexpr std::size_t kStatusBarBytes = 40;

[[nodiscard]] FixedStr<kClassicRowColumns + 1, StrFit::Clip> format_key_value(
    std::string_view name,
    std::string_view value
) noexcept;

class OsdRenderer {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit OsdRenderer(proto::OsdSurface& surface) noexcept : surface_(surface) {}

    void set_height(RowCount rows) noexcept { height_ = rows; }

    void set_visible_rows(proto::OsdRowCount n) noexcept { surface_.set_visible_rows(n); }

    RowCount height() const noexcept { return height_; }

    [[nodiscard]] bool in_height(OsdRow row) const noexcept {
        return row_index_of(row).before() < height_;
    }

    enum Arrow : std::uint8_t { kArrowNone = 0, kArrowLeft = 1, kArrowRight = 2 };

    void set_title(std::string_view s, Arrow arrow = kArrowNone) noexcept;

    void set_arrow(Arrow arrow) noexcept { arrow_ = arrow; }

    enum class Stripe : std::uint8_t { Title, Blank };

    void set_stripe(Stripe s) noexcept { stripe_ = s; }

    void publish_info_window(const proto::InfoWindow& w) noexcept {
        surface_.publish_info_window(w);
    }

    void clear();

    void text(
        OsdRow row,
        std::string_view s,
        bool inverted = false,
        bool stippled = false,
        std::optional<Glyph> leftchar = std::nullopt
    );
    void key_value(OsdRow row, std::string_view name, std::string_view value);
    void glyph(OsdRow row, unsigned col, Glyph g);
    void box(OsdRow top, OsdRow bottom);
    void bar(OsdRow row, unsigned col, std::uint32_t value, std::uint32_t max, unsigned width);

    void progress(OsdRow row, unsigned step);

    void scroll_stripes(bool more_above, bool more_below);

    void composite_status_bar(const StatusBarFacts& facts);

    proto::OsdSurface& surface() noexcept { return surface_; }

    std::uint32_t publishes() const noexcept { return publishes_; }

    std::uint32_t diffs_suppressed() const noexcept { return suppressed_; }

    std::uint32_t newline_refusals() const noexcept { return newline_refusals_; }

private:
    void stage_row(
        OsdRow row,
        std::string_view s,
        bool inverted,
        bool stippled,
        std::optional<Glyph> leftchar
    );
    void stage_from_surface(OsdRow row);
    void commit(OsdRow row);

    proto::OsdSurface& surface_;
    RowCount height_ = kFullOsdRows;
    std::uint8_t scratch_[proto::OsdSurface::kRowBytes]{};
    std::uint8_t title_[kTitleBytes]{};
    Arrow arrow_ = kArrowNone;
    Stripe stripe_ = Stripe::Title;
    std::uint32_t publishes_ = 0;
    std::uint32_t suppressed_ = 0;
    std::uint32_t newline_refusals_ = 0;
};

constexpr OsdRenderer::Arrow operator|(OsdRenderer::Arrow a, OsdRenderer::Arrow b) noexcept {
    return static_cast<OsdRenderer::Arrow>(
        static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b)
    );
}

}
