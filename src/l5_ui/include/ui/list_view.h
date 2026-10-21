// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "infra/seat.h"
#include "ui/types.h"

namespace mister::ui {

struct ViewportPolicy {
    bool lookahead = true;
    bool browse_expand = false;
};

struct Viewport {
    TASTY_SEAT_RESIDENT(Ui);
    RowIndex first{};
    RowIndex selected{};

    static Viewport centre(
        RowIndex selected,
        RowCount count,
        RowCount height,
        ViewportPolicy p
    ) noexcept;
};

template <class T>
class ListView {
    TASTY_SEAT_RESIDENT(Ui);

public:
    using NameOf = std::string_view (*)(const T&);

    ListView(std::span<const T> items, NameOf name_of, ViewportPolicy p) noexcept
        : items_(items),
          name_of_(name_of),
          policy_(p) {}

    std::size_t count() const noexcept { return items_.size(); }

    const Viewport& viewport() const noexcept { return view_; }

    const T* selected() const noexcept {
        return view_.selected.v < items_.size() ? &items_[view_.selected.v] : nullptr;
    }

    void set_height(RowCount h) noexcept { height_ = h; }

    void set_items(std::span<const T> items) noexcept { items_ = items; }

    void recentre() noexcept;

    void select(RowIndex i) noexcept;

    void next() noexcept;
    void prev() noexcept;
    void next_page() noexcept;
    void prev_page() noexcept;
    void home() noexcept;
    void end() noexcept;

    void next_group() noexcept;
    void prev_group() noexcept;

    void seek_letter(char c) noexcept;

    void select_named(std::string_view name) noexcept;

    RowCount page_step() const noexcept;

private:
    std::span<const T> items_;
    NameOf name_of_;
    ViewportPolicy policy_;
    Viewport view_{};
    RowCount height_{16};
};

}

#include "ui/list_view.inl"
