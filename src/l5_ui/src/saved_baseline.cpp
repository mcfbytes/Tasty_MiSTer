// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/saved_baseline.h"

#include "proto/conf_str.h"

namespace mister::ui {

void SavedBaseline::take(const proto::ItemTable& items, const StatusView& live) {
    TASTY_SEAT_BODY(SavedBaseline);
    if (held_)
        return;
    rebase(items, live);
}

void SavedBaseline::rebase(const proto::ItemTable& items, const StatusView& live) {
    TASTY_SEAT_BODY(SavedBaseline);
    fields_.clear();
    for (const proto::Item& it : items.items()) {
        if (it.kind == proto::ItemKind::Option && it.bits.valid())
            fields_.push_back(it.bits);
    }
    words_ = live;
    held_ = true;
}

void SavedBaseline::clear() noexcept {
    TASTY_SEAT_BODY(SavedBaseline);
    fields_.clear();
    words_ = StatusView{};
    held_ = false;
}

bool SavedBaseline::held() const noexcept {
    TASTY_SEAT_BODY(SavedBaseline);
    return held_;
}

bool SavedBaseline::unsaved(const StatusView& live) const noexcept {
    TASTY_SEAT_BODY(SavedBaseline);
    if (!held_)
        return false;
    for (const proto::BitField f : fields_) {
        if (words_.field(f) != live.field(f))
            return true;
    }
    return false;
}

}
