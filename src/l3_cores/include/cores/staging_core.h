// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <string_view>

#include "cores/mount_state.h"
#include "cores/stage_plan.h"
#include "infra/error.h"
#include "proto/link_op.h"
#include "proto/types.h"

namespace mister::cores {

inline constexpr std::chrono::nanoseconds kDiscMountBoundNs = std::chrono::seconds{5};

class IStagingCore {
public:
    virtual ~IStagingCore() = default;

    [[nodiscard]] virtual MountState mount_disc(std::string_view path) = 0;
    virtual void set_region(proto::RegionCode region) = 0;
    [[nodiscard]] virtual Ex<void> deferred_reset() = 0;

    virtual StagePlan stage_plan() noexcept { return {}; }

    [[nodiscard]] virtual Ex<void> stage_disc_payload(const proto::LinkOp::StageDiscPayload&) {
        return {};
    }

protected:
    IStagingCore() = default;
    IStagingCore(const IStagingCore&) = default;
    IStagingCore& operator=(const IStagingCore&) = default;
};

}
