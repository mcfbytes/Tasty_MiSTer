// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/sdram_latch.h"

#include "ui/page_ask.h"

namespace mister::ui {

void SdramLatch::on_mask(
    HdMask m,
    bool front_end,
    infra::OptRef<app::UiRequestRing> asks
) noexcept {
    TASTY_SEAT_BODY(SdramLatch);

    if (front_end && cfg_ == proto::OsdMask{} && m.osd_word().has(proto::kSdramPresent)) {
        cfg_ = m.osd_word();
    }

    if (front_end && cfg_ != proto::OsdMask{} && !reported_) {
        if (push_ask(asks, app::UiRequest::SdramDetected{.osd_mask = cfg_})) {
            reported_ = true;
        } else {
            ++refusals_;
        }
    }
}

}
