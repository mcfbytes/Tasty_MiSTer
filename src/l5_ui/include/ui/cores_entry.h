// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "svc/types.h"
#include "ui/core_pick_host.h"
#include "ui/page.h"
#include "ui/page_launches.h"

namespace mister::app {
class RecentsStore;
}

namespace mister::ui {

class IBrowseSource;
struct BrowserLaunch;
struct PageContext;

class CoresEntry final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    void bind(PageContext& ctx);

    Transition open(PageContext& ctx, bool recents_first);

    svc::Tally refusals() const noexcept {
        TASTY_SEAT_BODY(CoresEntry);
        return refusals_.sum(core_pick_.refusals());
    }

private:
    infra::OptRef<BrowserLaunch> browser_() const noexcept {
        return launches_ ? launches_->browser : infra::OptRef<BrowserLaunch>{};
    }

    svc::Tally refusals_{};
    infra::OptRef<PageLaunches> launches_{};
    infra::OptRef<const IBrowseSource> storage_{};
    infra::OptRef<app::RecentsStore> recents_{};
    CorePickHost core_pick_{};
};

}
