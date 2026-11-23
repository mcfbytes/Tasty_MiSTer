// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/serial.h"

namespace mister::app {

struct DiskAttachSeq {
    std::uint32_t v = 0;
    using serial_rule = infra::serial::SkipZero<std::uint32_t>;
    using serial_order = infra::serial::Order;

    [[nodiscard]] constexpr DiskAttachSeq next() const noexcept {
        return infra::serial::next(*this);
    }

    [[nodiscard]] constexpr bool ordered() const noexcept { return *this != DiskAttachSeq{}; }

    [[nodiscard]] constexpr bool at_or_past(DiskAttachSeq earlier) const noexcept {
        return infra::serial::at_or_past(*this, earlier.v);
    }

    friend constexpr bool operator==(DiskAttachSeq, DiskAttachSeq) = default;
};

static_assert(sizeof(DiskAttachSeq) == 4);

}
