// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/transfer_tracker.h"

#include <algorithm>
#include <string_view>

#include "app/ui_request_ring.h"
#include "infra/opt_ref.h"
#include "ui/page_context.h"

namespace mister::ui {

void TransferTracker::begin(const PageContext& ctx) noexcept {
    TASTY_SEAT_BODY(TransferTracker);
    if (in_transfer_)
        return;
    in_transfer_ = true;
    name_len_ = 0;
    const infra::OptRef<app::UiRequestRing> asks = ctx.ui_requests;
    if (!asks || asks->load_asks() == load_asks_seen_)
        return;
    load_asks_seen_ = asks->load_asks();
    const std::string_view leaf = progress_leaf(asks->last_load_path());
    std::copy(leaf.begin(), leaf.end(), name_);
    name_len_ = static_cast<std::uint8_t>(leaf.size());
}

}
