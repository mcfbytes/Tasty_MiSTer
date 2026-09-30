// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "proto/types.h"
#include "svc/storage_backend.h"

namespace mister::svc {

class IStorageLifecycle {
public:
    virtual ~IStorageLifecycle() = default;

    [[nodiscard]] virtual bool stage_backend(proto::SlotIndex slot,
                                             IStorageBackend& backend) noexcept = 0;

    [[nodiscard]] virtual bool release_slot(proto::SlotIndex slot) noexcept = 0;

    [[nodiscard]] virtual bool quiesced() const noexcept = 0;

protected:
    IStorageLifecycle() = default;
    IStorageLifecycle(const IStorageLifecycle&) = default;
    IStorageLifecycle& operator=(const IStorageLifecycle&) = default;
};

}  // namespace mister::svc
