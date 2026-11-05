// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/bay_releases.h"
#include "app/built_core.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/link_op.h"
#include "proto/types.h"

namespace mister::svc {
class IStorageLifecycle;
}

namespace mister::app {

struct Retiree {
    TASTY_SEAT_EXEMPT(component);
    proto::CoreBaySlot slot = proto::LinkOp::AdoptCore::kNoSlot;
    std::uint16_t gen = 0;
    infra::OptRef<svc::IStorageLifecycle> seat{};
    proto::BlockSlotMask mask{};
    proto::BlockSlotMask sent{};

    BayGens merged_gen{};

    void merge(const Retiree& from) noexcept {
        mask = mask.united(from.mask);
        sent = sent.common(from.sent);
        const BayGens from_gens = from.bay_gens();
        for (const proto::CoreBaySlot s : kAllBaySlots)
            if (infra::at_index(from_gens, s) != 0)
                infra::at_index(merged_gen, s) = infra::at_index(from_gens, s);
    }

    [[nodiscard]] BayGens bay_gens() const noexcept {
        BayGens out = merged_gen;
        if (in_bay(slot) && gen != 0)
            infra::at_index(out, slot) = gen;
        return out;
    }
};

}
