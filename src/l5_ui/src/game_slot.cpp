// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/game_slot.h"

#include <memory>

#include "app/session_identity.h"
#include "ui/options_model.h"
#include "ui/options_page.h"

namespace mister::ui {
namespace {

bool offered(const OptionItem& it) noexcept {
    return it.page == 0
        && it.screen_row.has_value()
        && it.selectable_index.has_value()
        && !it.disabled;
}

}

GameSlot::GameSlot(const OptionsModel& root, std::span<const cores::FileSlot> slots)
    : row_(nullptr),
      disc_row_(nullptr) {
    TASTY_SEAT_BODY(GameSlot);
    for (const std::unique_ptr<OptionItem>& it : root.items()) {
        if (offered(*it) && it->opens_browser() && !it->mounts_image()) {
            row_ = it.get();
            break;
        }
    }
    for (const std::unique_ptr<OptionItem>& it : root.items()) {
        if (!offered(*it) || !it->mounts_image())
            continue;
        if (cores::role_of(slots, proto::IoIndex{it->ioctl_index}) == cores::SlotRole::Disc) {
            disc_row_ = it.get();
            break;
        }
    }
    if (row_ == nullptr)
        row_ = disc_row_;
}

}
