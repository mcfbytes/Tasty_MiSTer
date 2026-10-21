// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_focus_memory.h"

namespace mister::ui {

std::optional<HdFocusMemory::Focus> HdFocusMemory::recall(PageId page, ItemId item) const noexcept {
    TASTY_SEAT_BODY(HdFocusMemory);
    for (std::uint8_t i = 0; i < count_; ++i) {
        if (entries_[i].page == page && entries_[i].item == item)
            return entries_[i].focus;
    }
    return std::nullopt;
}

void HdFocusMemory::remember(PageId page, ItemId item, Focus focus) noexcept {
    TASTY_SEAT_BODY(HdFocusMemory);
    for (std::uint8_t i = 0; i < count_; ++i) {
        if (entries_[i].page == page && entries_[i].item == item) {
            entries_[i].focus = focus;
            return;
        }
    }
    if (count_ < kEntries) {
        entries_[count_++] = Entry{page, item, focus};
        return;
    }
    entries_[oldest_] = Entry{page, item, focus};
    oldest_ = static_cast<std::uint8_t>((oldest_ + 1) % kEntries);
    ++evictions_;
}

void HdFocusMemory::clear() noexcept {
    TASTY_SEAT_BODY(HdFocusMemory);
    count_ = 0;
    oldest_ = 0;
}

}
