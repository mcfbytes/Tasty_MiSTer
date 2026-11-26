// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "hal/link_halves.h"
#include "proto/disk_register_count.h"
#include "proto/types.h"

namespace mister::proto {

struct DiskAddress {
    std::uint32_t v = 0;
    using affine_count = DiskRegisterCount;

    static constexpr FioWordCount kWords{2};

    [[nodiscard]] constexpr hal::LinkHalves halves() const noexcept {
        return hal::LinkHalves::of(v);
    }

    friend constexpr bool operator==(DiskAddress, DiskAddress) = default;
};

static_assert(sizeof(DiskAddress) == 4);

}
