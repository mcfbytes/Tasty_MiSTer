// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/hd_osd_status.h"
#include "infra/json_out.h"
#include "infra/opt_ref.h"
#include "record_ctx.h"

namespace mister::fw {

struct HdRecord {
    static constexpr infra::JsonName kKey{"hd"};
    using View = app::HdOsdStatus;
    infra::OptRef<const app::HdOsdStatusCell> cell{};

    [[nodiscard]] std::optional<View> sample(const RecordCtx&) const noexcept {
        return sample_published(cell);
    }
};

}
