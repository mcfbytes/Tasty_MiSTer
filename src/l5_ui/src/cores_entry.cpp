// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/cores_entry.h"

#include <optional>

#include "app/recents_store.h"
#include "infra/opt_ref.h"
#include "ui/browse_source.h"
#include "ui/browser_host.h"
#include "ui/browser_launch.h"
#include "ui/browser_session.h"
#include "ui/page_context.h"
#include "ui/page_launches.h"

namespace mister::ui {

void CoresEntry::bind(PageContext& ctx) {
    TASTY_SEAT_BODY(CoresEntry);
    launches_ = infra::OptRef<PageLaunches>{ctx.launches};
    storage_ = ctx.storage;
    recents_ = ctx.recents;
    core_pick_.bind({.route = ctx.route, .recents = ctx.recents});
}

Transition CoresEntry::open(PageContext& ctx, bool recents_first) {
    TASTY_SEAT_BODY(CoresEntry);
    bind(ctx);
    const infra::OptRef<BrowserLaunch> browser = browser_();
    if (!browser || !storage_) {
        refusals_.add();
        return Transition{};
    }
    BrowserSession::Request r;
    r.extensions = "RBFMRAMGL";
    r.scan.cores_only = true;
    const app::RecentsStore::ListId cores_list = app::RecentsStore::ListId::cores();
    r.recents = cores_list;
    r.recents_first = recents_first;
    r.host = infra::opt_ref_of(static_cast<IBrowserHost*>(&core_pick_));
    if (recents_first) {

        if (!recents_ || recents_->load(app::RecentsStore::kNoCore, cores_list).empty()) {
            refusals_.add();
            return Transition{};
        }
    }
    return browser->open(PageId::Browser, static_cast<BrowserSession::Request&&>(r));
}

}
