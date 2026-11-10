// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "app/mgl_item.h"
#include "app/mgl_player.h"
#include "app/remembered_path.h"
#include "infra/seat.h"
#include "proto/conf_str.h"

namespace mister::app {

struct SlotRemember {
    TASTY_SEAT_RESIDENT(Ui);
    MglPlayer& player;
    const std::vector<proto::ConfStrEntry>& slots;
    const RememberedStem& stem;

    void on(const MglItem::Load& load, const MglItem::Head&, MglItemNo at) noexcept {
        TASTY_SEAT_BODY(SlotRemember);
        const bool file = load.slot == MglItem::Slot::File;
        const proto::ConfStrEntry::SlotLetter letter = file
            ? proto::ConfStrEntry::SlotLetter::Load
            : proto::ConfStrEntry::SlotLetter::Mount;
        const proto::FileSlotHit hit = proto::ConfStr::find_slot_in(slots, letter, load.index);

        if (!hit.entry || !hit.entry->store_name)
            return;
        if (!file && hit.how != proto::FileSlotMatch::Exact)
            return;
        player.remember(
            at,
            stem,
            file ? RememberedSlot::File : RememberedSlot::Mount,
            hit.entry->ioctl_index
        );
    }

    void on(const MglItem::Reset&, const MglItem::Head&, MglItemNo) noexcept {
        TASTY_SEAT_BODY(SlotRemember);
    }

    void misrouted(const MglItem&, MglItemNo) noexcept { TASTY_SEAT_BODY(SlotRemember); }
};

}
