// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/mount_state.h"
#include "infra/error.h"

namespace mister::cores {

[[nodiscard]] constexpr Error mount_declined(infra::ErrorSite site) noexcept {
    return Error{Errc::would_block, site, 0};
}

[[nodiscard]] constexpr MountState mount_answer_of(const Error& refusal) noexcept {
    return refusal.code == Errc::would_block ? MountState::Pending : MountState::Failed;
}

}
