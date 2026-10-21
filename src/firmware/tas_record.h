// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/replay_status.h"
#include "infra/json_out.h"
#include "record_ctx.h"

namespace mister::fw {

struct TasRecord {
    static constexpr infra::JsonName kKey{"tas"};
    using View = app::ReplayStatus;
    const app::ReplayStatusCell& cell;

    [[nodiscard]] std::optional<View> sample(const RecordCtx&) const noexcept {
        return sample_published(cell);
    }
};

}
