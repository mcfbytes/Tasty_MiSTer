// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/types.h"

namespace mister::ui {

struct ViewportPolicy {
    bool lookahead = true;
    bool browse_expand = false;
};

struct ListWindow {
    RowCount items{};
    RowCount height{};
};

struct Viewport {
    TASTY_SEAT_RESIDENT(Ui);
    RowIndex first{};
    RowIndex selected{};

    static Viewport centre(RowIndex selected, ListWindow window, ViewportPolicy p) noexcept;
};

enum class Wrap : bool { No, Yes };

template <class T>
class ListView {
    TASTY_SEAT_RESIDENT(Ui);

public:
    ListView(std::span<const T> items, ViewportPolicy p = {}) noexcept
        : items_(RowSpan<const T>{items}),
          policy_(p) {}

    RowCount count() const noexcept { return items_.size(); }

    const Viewport& viewport() const noexcept { return view_; }

    infra::OptRef<const T> selected() const noexcept {
        return infra::opt_ref_of<const T>(
            view_.selected < row_after(items_.size()) ? &items_[view_.selected] : nullptr
        );
    }

    void set_height(RowCount h) noexcept { height_ = h; }

    void set_items(std::span<const T> items) noexcept { items_ = RowSpan<const T>{items}; }

    void recentre() noexcept;

    void select(RowIndex i) noexcept;

    void next() noexcept;
    void prev() noexcept;
    void next_page() noexcept;
    void prev_page() noexcept;
    void home() noexcept;
    void end() noexcept;

    template <class Pred>
    bool select_next(Pred&& pred, Wrap wrap) noexcept;

    void next_group() noexcept;
    void prev_group() noexcept;

    void seek_letter(char c) noexcept;

    void select_named(std::string_view name) noexcept;

    RowCount page_step() const noexcept;

private:
    RowSpan<const T> items_;
    ViewportPolicy policy_;
    Viewport view_{};
    RowCount height_{kFullOsdRows};
};

}

#include "ui/list_view.inl"
