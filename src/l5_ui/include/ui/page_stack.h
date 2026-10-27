// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "infra/seat.h"
#include "ui/page.h"
#include "ui/types.h"

namespace mister::ui {

class PageStack {
    TASTY_SEAT_RESIDENT(Ui);

public:
    bool empty() const noexcept { return pages_.empty(); }

    std::size_t depth() const noexcept { return pages_.size(); }

    Page* top() noexcept { return pages_.empty() ? nullptr : pages_.back().get(); }

    const Page* top() const noexcept { return pages_.empty() ? nullptr : pages_.back().get(); }

    PageId top_id() const noexcept { return pages_.empty() ? PageId::None : pages_.back()->id(); }

    PageId bottom_id() const noexcept {
        return pages_.empty() ? PageId::None : pages_.front()->id();
    }

    const Page* bottom() const noexcept { return pages_.empty() ? nullptr : pages_.front().get(); }

    PageId beneath_top_id() const noexcept {
        return pages_.size() < 2 ? PageId::None : pages_[pages_.size() - 2]->id();
    }

    void push(std::unique_ptr<Page> p);
    ItemId pop();
    void replace(std::unique_ptr<Page> p);
    void clear() noexcept;

    void set_pending_focus(ItemId f) noexcept { pending_focus_ = f; }

    std::uint32_t page_refusals() const noexcept;

    bool any_bt_pairing() const noexcept;

    bool any_runs_a_process() const noexcept;

    bool any_takes_focus() const noexcept;

    const Page* focus_top() const noexcept;

private:
    std::vector<std::unique_ptr<Page>> pages_;
    ItemId pending_focus_ = kNoItem;
    std::uint32_t retired_refusals_ = 0;
};

}
