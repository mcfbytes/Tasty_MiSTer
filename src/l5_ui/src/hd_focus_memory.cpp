// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_focus_memory.h"

namespace mister::ui {

std::optional<HdFocusMemory::Focus> HdFocusMemory::recall(FocusKey key) const noexcept {
    TASTY_SEAT_BODY(HdFocusMemory);
    for (std::uint8_t i = 0; i < count_; ++i) {
        if (entries_[i].matches(key))
            return entries_[i].focus;
    }
    return std::nullopt;
}

void HdFocusMemory::remember(FocusKey key, Focus focus) noexcept {
    TASTY_SEAT_BODY(HdFocusMemory);
    for (std::uint8_t i = 0; i < count_; ++i) {
        if (entries_[i].matches(key)) {
            entries_[i].focus = focus;
            return;
        }
    }
    if (count_ < kEntries) {
        entries_[count_++] = Entry{key, focus};
        return;
    }
    entries_[oldest_] = Entry{key, focus};
    oldest_ = next_slot(oldest_);
    ++evictions_;
}

void HdFocusMemory::clear() noexcept {
    TASTY_SEAT_BODY(HdFocusMemory);
    count_ = 0;
    oldest_ = 0;
}

}
