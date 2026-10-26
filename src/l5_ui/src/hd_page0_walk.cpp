// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_page0_walk.h"

#include "ui/options_page.h"
#include "ui/place_facts.h"

namespace mister::ui {

BuildEnv HdPage0Walk::page0_of(const BuildEnv& env) noexcept {
    BuildEnv page0 = env;
    page0.page = 0;
    page0.flat = false;
    return page0;
}

SlotFocus HdPage0Walk::focus_of(const OptionItem& it) const {
    TASTY_SEAT_BODY(HdPage0Walk);
    return SlotFocus{
        it.mounts_image() ? SlotFocus::Kind::Image : SlotFocus::Kind::File,
        proto::IoIndex{it.ioctl_index}
    };
}

bool HdPage0Walk::run(
    const proto::ItemTable& table,
    const BuildEnv& env,
    bool attempt,
    const CoreVerbs& verbs
) {
    TASTY_SEAT_BODY(HdPage0Walk);
    const BuildEnv page0 = page0_of(env);
    placer_.reset();
    if (attempt && model_.build(table, page0).has_value()) {
        placer_.emplace(model_, page0.slots, verbs, PlaceFacts{});
    }
    return placer_.has_value();
}

infra::OptRef<const OptionItem> HdPage0Walk::slot_row(SlotFocus want) const {
    TASTY_SEAT_BODY(HdPage0Walk);
    if (!placer_)
        return {};
    for (const OptionItem* it : placer_->slots()) {
        if (focus_of(*it) == want)
            return infra::OptRef<const OptionItem>{*it};
    }
    return {};
}

}
