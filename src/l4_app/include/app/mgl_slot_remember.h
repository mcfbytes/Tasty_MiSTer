// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "app/mgl_item.h"
#include "app/mgl_player.h"
#include "app/remembered_path.h"
#include "infra/seat.h"
#include "proto/file_slots.h"

namespace mister::app {

struct SlotRemember {
    TASTY_SEAT_RESIDENT(Ui);
    MglPlayer& player;
    const proto::FileSlots& slots;
    const RememberedStem& stem;

    void on(const MglItem::Load& load, const MglItem::Head&, MglItemNo at) noexcept {
        TASTY_SEAT_BODY(SlotRemember);
        const proto::FileSlotHit hit = slots.find(load.slot, load.index);
        if (!hit.entry || !hit.entry->store_name)
            return;
        const proto::FileRowId row = hit.entry->file_row();

        if (row.kind == proto::FileRowKind::Mount && hit.how != proto::FileSlotMatch::Exact)
            return;
        player.remember(at, stem, row);
    }

    void on(const MglItem::Reset&, const MglItem::Head&, MglItemNo) noexcept {
        TASTY_SEAT_BODY(SlotRemember);
    }

    void misrouted(const MglItem&, MglItemNo) noexcept { TASTY_SEAT_BODY(SlotRemember); }
};

}
