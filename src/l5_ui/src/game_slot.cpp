// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/game_slot.h"

#include <memory>
#include <optional>

#include "app/session_identity.h"
#include "ui/options_model.h"
#include "ui/option_item.h"

namespace mister::ui {
namespace {

bool offered(const OptionItem& it) noexcept {
    return it.page == proto::kRootSubmenu
        && it.drawn_row.has_value()
        && it.selectable_index.has_value()
        && !it.disabled;
}

}

GameSlot::GameSlot(const OptionsModel& root, std::span<const cores::FileSlot> slots) {
    TASTY_SEAT_BODY(GameSlot);
    for (const std::unique_ptr<OptionItem>& it : root.items()) {
        const std::optional<proto::FileRowId> r = it->file_row();
        if (offered(*it) && r && r->kind == proto::FileRowKind::File) {
            row_ = infra::OptRef<const OptionItem>{*it};
            break;
        }
    }
    for (const std::unique_ptr<OptionItem>& it : root.items()) {
        if (!offered(*it))
            continue;
        const std::optional<proto::FileRowId> r = it->file_row();
        if (!r || r->kind != proto::FileRowKind::Mount)
            continue;
        if (cores::role_of(slots, it->ioctl_index) == cores::SlotRole::Disc) {
            disc_row_ = infra::OptRef<const OptionItem>{*it};
            break;
        }
    }
    if (!row_)
        row_ = disc_row_;
}

}
