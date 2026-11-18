// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/st_loader.h"

#include "cores/st_slot.h"
#include "infra/error.h"

namespace mister::cores {

Ex<LoadPlan> StLoader::plan(const LoadAsk& ask, const proto::StatusWord& status) {
    TASTY_SEAT_BODY(StLoader);
    (void)status;
    const auto slot = st_ctrl::StSlot::of(ask.index.io());
    if (slot == st_ctrl::StSlot::Tos)
        return tos_plan_(ask);
    if (slot == st_ctrl::StSlot::Cart)
        return cart_plan_(ask);
    return unimplemented(ERR_SITE());
}

Ex<LoadPlan> StLoader::tos_plan_(const LoadAsk& ask) {
    LoadPlan plan{};
    proto::WideIoIndex index{};
    if (ask.size >= kTosLargeBytes) {
        index = kTosLargeIndex;
    } else if (ask.size == kTosSmallBytes) {
        index = kTosSmallIndex;
    } else {
        return plan;
    }
    plan.rows[0] = TransferRow{
        .dest = RowDest::Payload,
        .index = index,
        .source = ask.path,
        .file_offset = proto::kFileStart,
        .file_len = ask.size,
        .extent = ask.size
    };
    plan.count = 1;
    return plan;
}

Ex<LoadPlan> StLoader::cart_plan_(const LoadAsk& ask) {
    LoadPlan plan{};
    if (ask.dongle || ask.size.empty()) {
        plan.rows[0] = TransferRow{
            .dest = RowDest::Payload,
            .index = kCartIndex,
            .fill = kBlankFill,
            .extent = kCartBytes
        };
        plan.count = 1;
        return plan;
    }
    if (ask.size != kCartBufferBytes) {
        return std::unexpected(ask.size.refusal(Errc::bad_format, ERR_SITE()));
    }
    plan.rows[0] = TransferRow{
        .dest = RowDest::Payload,
        .index = kCartIndex,
        .source = ask.path,
        .file_offset = proto::kFileStart + kCartHeaderBytes,
        .file_len = kCartBytes,
        .extent = kCartBytes
    };
    plan.count = 1;
    return plan;
}

}
