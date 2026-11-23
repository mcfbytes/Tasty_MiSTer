// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/bay_releases.h"
#include "app/built_core.h"
#include "app/disk_attach_seq.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/build_generation.h"
#include "proto/link_op.h"
#include "proto/types.h"

namespace mister::svc {
class IStorageLifecycle;
}

namespace mister::app {

struct Retiree {
    TASTY_SEAT_EXEMPT(component);
    proto::CoreBaySlot slot = proto::LinkOp::AdoptCore::kNoSlot;
    proto::BuildGeneration gen{};
    infra::OptRef<svc::IStorageLifecycle> seat{};
    proto::BlockSlotMask mask{};
    proto::BlockSlotMask sent{};

    DiskAttachSeq disks{};

    BayGens merged_gen{};

    void merge(const Retiree& from) noexcept {
        mask = mask.united(from.mask);
        sent = sent.common(from.sent);

        if (!disks.at_or_past(from.disks))
            disks = from.disks;
        const BayGens from_gens = from.bay_gens();
        for (const proto::CoreBaySlot s : kAllBaySlots)
            if (infra::at_index(from_gens, s).built())
                infra::at_index(merged_gen, s) = infra::at_index(from_gens, s);
    }

    [[nodiscard]] bool held() const noexcept { return !mask.empty() || disks.ordered(); }

    [[nodiscard]] BayGens bay_gens() const noexcept {
        BayGens out = merged_gen;
        if (in_bay(slot) && gen.built())
            infra::at_index(out, slot) = gen;
        return out;
    }
};

}
