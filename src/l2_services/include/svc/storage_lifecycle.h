// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "infra/opt_ref.h"
#include "proto/types.h"
#include "svc/storage_backend.h"

namespace mister::svc {

struct BlankSaveSpec;

class IStorageLifecycle {
public:
    virtual ~IStorageLifecycle() = default;

    [[nodiscard]] virtual bool stage_backend(
        proto::SlotIndex slot,
        IStorageBackend& backend,
        std::span<const std::byte> order,
        infra::OptRef<const BlankSaveSpec> blank
    ) noexcept = 0;

    [[nodiscard]] virtual bool release_slot(proto::SlotIndex slot) noexcept = 0;

    [[nodiscard]] virtual bool quiesced() const noexcept = 0;

protected:
    IStorageLifecycle() = default;
    IStorageLifecycle(const IStorageLifecycle&) = default;
    IStorageLifecycle& operator=(const IStorageLifecycle&) = default;
};

}
