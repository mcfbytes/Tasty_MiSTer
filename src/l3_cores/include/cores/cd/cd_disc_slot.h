// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "svc/disc/disc_read_service.h"

namespace mister::cores::cd {

class CdEngine;

class CdDiscSlot {
    TASTY_SEAT_RESIDENT(RT);

public:
    CdDiscSlot() = default;
    CdDiscSlot(const CdDiscSlot&) = delete;
    CdDiscSlot& operator=(const CdDiscSlot&) = delete;

    void adopt(infra::OptRef<svc::disc::DiscReadService> granted) noexcept;

    svc::disc::DiscMountState refresh(infra::OptRef<CdEngine> engine) noexcept;

    [[nodiscard]] svc::disc::DiscArm arm_mount(
        svc::disc::DiscOp op,
        infra::OptRef<const svc::disc::CuePolicy> policy,
        std::string_view path
    ) noexcept;

    [[nodiscard]] bool bound() const noexcept { return discs_.has_value(); }

    [[nodiscard]] infra::OptRef<svc::disc::DiscReadService> discs() const noexcept {
        return discs_;
    }

    [[nodiscard]] const svc::disc::DiscGeometry& geometry() const noexcept { return geom_; }

    [[nodiscard]] std::optional<svc::disc::DiscCounters> counters() const noexcept;
    [[nodiscard]] std::uint32_t not_resident() const noexcept;

private:
    svc::disc::DiscGeometry geom_{};
    svc::disc::DiscCounters counters_{};

    infra::OptRef<svc::disc::DiscReadService> discs_{};
    svc::disc::DiscMountState mount_seen_ = svc::disc::DiscMountState::Idle;
};

}
