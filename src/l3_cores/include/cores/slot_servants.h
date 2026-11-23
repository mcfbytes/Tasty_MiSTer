// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/opt_ref.h"
#include "proto/types.h"
#include "proto/window_geometry.h"
#include "svc/storage_backend.h"

namespace mister::cores {

class ISlotServants {
public:
    virtual ~ISlotServants() = default;

    [[nodiscard]] virtual infra::OptRef<svc::IStorageBackend> servant(
        proto::SlotIndex slot
    ) noexcept = 0;

    [[nodiscard]] virtual const proto::IWindowGeometry& window_geometry() const noexcept = 0;

protected:
    ISlotServants() = default;
    ISlotServants(const ISlotServants&) = default;
    ISlotServants& operator=(const ISlotServants&) = default;
};

}
