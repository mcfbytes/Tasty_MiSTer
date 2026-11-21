// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd/cd_disc_slot.h"

#include <optional>
#include <string_view>

#include "cores/cd/cd_engine.h"

namespace mister::cores::cd {

void CdDiscSlot::adopt(infra::OptRef<svc::disc::DiscReadService> granted) noexcept {
    discs_ = granted;

    geom_ = discs_->geometry();
}

svc::disc::DiscMountState CdDiscSlot::refresh(infra::OptRef<CdEngine> engine) noexcept {
    if (!discs_)
        return svc::disc::DiscMountState::Failed;
    const svc::disc::DiscMountState st = discs_->mount_state();
    if (st != mount_seen_) {
        mount_seen_ = st;
        if (st == svc::disc::DiscMountState::Mounted || st == svc::disc::DiscMountState::Failed) {
            geom_ = discs_->geometry();
            if (engine) {
                engine->notify_disc(st == svc::disc::DiscMountState::Mounted, geom_);
            }
        }
    }
    counters_ = discs_->counters();
    return st;
}

svc::disc::DiscArm CdDiscSlot::arm_mount(
    svc::disc::DiscOp op,
    infra::OptRef<const svc::disc::CuePolicy> policy,
    std::string_view path
) noexcept {
    return discs_->arm_mount(op, policy, path);
}

std::optional<svc::disc::DiscCounters> CdDiscSlot::counters() const noexcept {
    TASTY_SEAT_BODY(CdDiscSlot);
    if (!discs_)
        return std::nullopt;
    return counters_;
}

std::uint32_t CdDiscSlot::not_resident() const noexcept {
    return discs_ ? discs_->not_resident() : 0u;
}

}
