// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "proto/osd_surface.h"
#include "proto/types.h"
#include "ui/types.h"
#include "infra/seat.h"

namespace mister::ui {

struct StatusBarFacts;

using proto::OsdRow;

enum class Glyph : std::uint8_t {
    SubmenuArrow = 0x16,
    ArrowUp = 0x11,
    ArrowDown = 0x10,
    BarFilled = 0x7F,
    BarEmpty = 0x8C,
};

inline constexpr unsigned kOsdColumns = 32;

inline constexpr unsigned kSideStripeBytes = 22;

inline constexpr unsigned kProgressMax = 167;

inline constexpr unsigned kOptionRowWidth = 28;

std::size_t format_key_value(std::span<char> out, std::string_view name, std::string_view value);

class OsdRenderer {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit OsdRenderer(proto::OsdSurface& surface) noexcept : surface_(&surface) {}

    void set_height(RowCount rows) noexcept { height_ = rows; }

    void set_visible_rows(unsigned n) noexcept {
        if (surface_ != nullptr)
            surface_->set_visible_rows(n);
    }

    RowCount height() const noexcept { return height_; }

    enum Arrow : std::uint8_t { kArrowNone = 0, kArrowLeft = 1, kArrowRight = 2 };

    void set_title(std::string_view s, Arrow arrow = kArrowNone) noexcept;

    void set_arrow(Arrow arrow) noexcept { arrow_ = arrow; }

    enum class Stripe : std::uint8_t { Title, Blank };

    void set_stripe(Stripe s) noexcept { stripe_ = s; }

    void clear();

    void text(
        OsdRow row,
        std::string_view s,
        bool inverted = false,
        bool stippled = false,
        std::uint8_t leftchar = 0
    );
    void key_value(OsdRow row, std::string_view name, std::string_view value);
    void glyph(OsdRow row, unsigned col, Glyph g);
    void box(OsdRow top, OsdRow bottom);
    void bar(OsdRow row, unsigned col, std::uint32_t value, std::uint32_t max, unsigned width);

    void progress(OsdRow row, unsigned step);

    void scroll_stripes(bool more_above, bool more_below);

    void composite_status_bar(const StatusBarFacts& facts);

    proto::OsdSurface& surface() noexcept { return *surface_; }

    std::uint32_t publishes() const noexcept { return publishes_; }

    std::uint32_t diffs_suppressed() const noexcept { return suppressed_; }

    std::uint32_t newline_refusals() const noexcept { return newline_refusals_; }

private:
    void stage_row(
        OsdRow row,
        std::string_view s,
        bool inverted,
        bool stippled,
        std::uint8_t leftchar
    );
    void stage_from_surface(OsdRow row);
    void commit(OsdRow row);

    proto::OsdSurface* surface_;
    RowCount height_{16};
    std::uint8_t scratch_[proto::OsdSurface::kRowBytes]{};
    std::uint8_t title_[256]{};
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
