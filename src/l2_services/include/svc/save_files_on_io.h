// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/opt_ref.h"
#include "proto/types.h"
#include "svc/storage_backend.h"
#include "svc/types.h"

namespace mister::svc {

struct BlankSaveSpec;

class ISaveFilesOnIo {
public:
    struct Save {
        infra::OptRef<IStorageBackend> file{};

        std::optional<proto::FileSize> extent{};
        bool growable = false;
        Generation gen{};

        infra::OptRef<const BlankSaveSpec> blank{};
    };

    [[nodiscard]] virtual Save save_on_io(proto::SlotIndex slot) const noexcept = 0;

protected:
    ISaveFilesOnIo() = default;
    ISaveFilesOnIo(const ISaveFilesOnIo&) = default;
    ISaveFilesOnIo& operator=(const ISaveFilesOnIo&) = default;
    ~ISaveFilesOnIo() = default;
};

}
