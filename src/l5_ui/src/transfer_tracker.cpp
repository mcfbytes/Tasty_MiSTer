// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/transfer_tracker.h"

#include "app/ui_request_ring.h"
#include "infra/opt_ref.h"
#include "ui/page_context.h"

namespace mister::ui {

void TransferTracker::begin(const PageContext& ctx) noexcept {
    TASTY_SEAT_BODY(TransferTracker);
    if (in_transfer_)
        return;
    in_transfer_ = true;
    name_.clear();
    const infra::OptRef<app::UiRequestRing> asks = ctx.ui_requests;
    if (!asks || asks->load_asks() == load_asks_seen_)
        return;
    load_asks_seen_ = asks->load_asks();
    (void)name_.assign(progress_leaf(asks->last_load_path()));
}

}
