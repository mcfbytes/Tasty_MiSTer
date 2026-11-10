// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/navigator.h"

#include <optional>

namespace mister::ui {

bool Navigator::selectable(RowIndex i) const noexcept {
    return mask_.has(i);
}

bool Navigator::move(Direction d) noexcept {
    if (mask_ == MenuMask{})
        return false;

    const RowIndex before = index_;
    std::optional<RowIndex> to = d == Direction::Down ? mask_.next_after(index_)
                                                      : mask_.prev_before(index_);
    if (!to)
        to = d == Direction::Down ? mask_.lowest() : mask_.highest();
    if (to)
        index_ = *to;
    return index_ != before;
}

bool Navigator::move_tier(Direction d, MenuMask run) noexcept {
    const MenuMask out = mask_.without(run);
    if (out == MenuMask{} || run == MenuMask{})
        return false;
    const RowIndex before = index_;
    std::optional<RowIndex> to;
    if (d == Direction::Down) {
        to = out.next_after(*run.highest());
        if (!to)
            to = out.lowest();
    } else {
        to = out.prev_before(*run.lowest());
        if (!to)
            to = out.highest();
    }
    if (to)
        index_ = *to;
    return index_ != before;
}

bool Navigator::move_strip(Direction d, MenuMask run) noexcept {
    const MenuMask in = mask_.only(run);
    if (in == MenuMask{})
        return false;
    if (!MenuMask::addresses(index_))
        return false;
    const std::optional<RowIndex> to = d == Direction::Down ? in.next_after(index_)
                                                            : in.prev_before(index_);
    if (!to)
        return false;
    index_ = *to;
    return true;
}

}
