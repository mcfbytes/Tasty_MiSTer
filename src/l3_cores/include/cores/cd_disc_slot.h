// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "svc/disc_read_service.h"

namespace mister::cores {

class CdEngine;

class CdDiscSlot {
    TASTY_SEAT_RESIDENT(RT);

public:
    CdDiscSlot() = default;
    CdDiscSlot(const CdDiscSlot&) = delete;
    CdDiscSlot& operator=(const CdDiscSlot&) = delete;

    void adopt(infra::OptRef<svc::DiscReadService> granted) noexcept;

    svc::DiscMountState refresh(infra::OptRef<CdEngine> engine) noexcept;

    [[nodiscard]] svc::DiscMountState arm_mount(
        svc::DiscOp op,
        const svc::CuePolicy* policy,
        std::string_view path
    ) noexcept;

    [[nodiscard]] bool bound() const noexcept { return discs_.has_value(); }

    [[nodiscard]] infra::OptRef<svc::DiscReadService> discs() const noexcept { return discs_; }

    [[nodiscard]] const svc::DiscGeometry& geometry() const noexcept { return geom_; }

    [[nodiscard]] std::optional<svc::DiscCounters> counters() const noexcept;
    [[nodiscard]] std::uint32_t not_resident() const noexcept;

private:
    svc::DiscGeometry geom_{};
    svc::DiscCounters counters_{};

    infra::OptRef<svc::DiscReadService> discs_{};
    svc::DiscMountState mount_seen_ = svc::DiscMountState::Idle;
};

}
