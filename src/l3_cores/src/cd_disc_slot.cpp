// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd_disc_slot.h"

#include <optional>
#include <string_view>

#include "cores/cd_engine.h"

namespace mister::cores {

void CdDiscSlot::adopt(infra::OptRef<svc::DiscReadService> granted) noexcept {
    discs_ = granted;

    geom_ = discs_->geometry();
}

svc::DiscMountState CdDiscSlot::refresh(infra::OptRef<CdEngine> engine) noexcept {
    if (!discs_)
        return svc::DiscMountState::Failed;
    const svc::DiscMountState st = discs_->mount_state();
    if (st != mount_seen_) {
        mount_seen_ = st;
        if (st == svc::DiscMountState::Mounted || st == svc::DiscMountState::Failed) {
            geom_ = discs_->geometry();
            if (engine) {
                (void)engine->notify_disc(st == svc::DiscMountState::Mounted);
            }
        }
    }
    counters_ = discs_->counters();
    return st;
}

svc::DiscMountState CdDiscSlot::arm_mount(
    svc::DiscOp op,
    const svc::CuePolicy* policy,
    std::string_view path
) noexcept {
    return discs_->arm_mount(op, policy, path);
}

std::optional<svc::DiscCounters> CdDiscSlot::counters() const noexcept {
    TASTY_SEAT_BODY(CdDiscSlot);
    if (!discs_)
        return std::nullopt;
    return counters_;
}

std::uint32_t CdDiscSlot::not_resident() const noexcept {
    return discs_ ? discs_->not_resident() : 0u;
}

}
