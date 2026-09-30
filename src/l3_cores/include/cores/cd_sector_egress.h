// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "cores/core_profile.h"
#include "infra/error.h"

namespace mister::cores {

class ICdSectorEgress {
public:
    struct Act {
        IoIndex sub_index{};
        std::span<const std::uint8_t> subcode{};
        IoIndex index{};
        std::span<const std::uint8_t> sector{};
    };

    virtual ~ICdSectorEgress() = default;

    [[nodiscard]] virtual bool egress_idle() const noexcept = 0;
    [[nodiscard]] virtual Ex<void> submit_sector(const Act& act) = 0;

protected:
    ICdSectorEgress() = default;
    ICdSectorEgress(const ICdSectorEgress&) = default;
    ICdSectorEgress& operator=(const ICdSectorEgress&) = default;
};

}  // namespace mister::cores
