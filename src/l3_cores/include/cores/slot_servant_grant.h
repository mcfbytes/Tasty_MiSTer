// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "proto/block_geometry.h"
#include "proto/types.h"

namespace mister::svc {
class IStorageBackend;
}

namespace mister::cores {

struct SlotServantGrant {
    infra::IndexedArray<
        infra::OptRef<svc::IStorageBackend>,
        proto::kAnnounceableSlots,
        proto::SlotIndex>
        by_slot{};

    [[nodiscard]] infra::OptRef<svc::IStorageBackend> servant(
        proto::SlotIndex slot
    ) const noexcept {
        const auto lent = by_slot.find(slot);
        return lent ? *lent : infra::OptRef<svc::IStorageBackend>{};
    }
};

}
