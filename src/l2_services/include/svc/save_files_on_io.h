// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/opt_ref.h"
#include "proto/types.h"
#include "svc/storage_backend.h"

namespace mister::svc {

class ISaveFilesOnIo {
public:
    struct Save {
        infra::OptRef<IStorageBackend> file{};
        proto::FileSize size{};
        bool exists = false;
        bool growable = false;
        std::uint32_t gen = 0;
    };

    [[nodiscard]] virtual Save save_on_io(proto::SlotIndex slot) const noexcept = 0;

protected:
    ISaveFilesOnIo() = default;
    ISaveFilesOnIo(const ISaveFilesOnIo&) = default;
    ISaveFilesOnIo& operator=(const ISaveFilesOnIo&) = default;
    ~ISaveFilesOnIo() = default;
};

}
