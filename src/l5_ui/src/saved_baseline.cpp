// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/saved_baseline.h"

#include "proto/conf_str.h"

namespace mister::ui {

void SavedBaseline::take(const proto::ItemTable& items, const StatusView& live) {
    TASTY_SEAT_BODY(SavedBaseline);
    if (held_.has_value())
        return;
    rebase(items, live);
}

void SavedBaseline::rebase(const proto::ItemTable& items, const StatusView& live) {
    TASTY_SEAT_BODY(SavedBaseline);
    if (!held_.has_value())
        held_.emplace();
    Held& base = *held_;
    base.fields.clear();
    for (const proto::Item& it : items.items()) {
        if (it.kind == proto::ItemKind::Option && it.bits.valid())
            base.fields.push_back(it.bits);
    }
    base.words = live;
}

void SavedBaseline::clear() noexcept {
    TASTY_SEAT_BODY(SavedBaseline);
    held_.reset();
}

bool SavedBaseline::held() const noexcept {
    TASTY_SEAT_BODY(SavedBaseline);
    return held_.has_value();
}

bool SavedBaseline::unsaved(const StatusView& live) const noexcept {
    TASTY_SEAT_BODY(SavedBaseline);
    if (!held_.has_value())
        return false;
    const Held& base = *held_;
    for (const proto::BitField f : base.fields) {
        if (base.words.field(f) != live.field(f))
            return true;
    }
    return false;
}

}
