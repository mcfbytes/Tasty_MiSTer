// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/script_pick_host.h"

#include <string>

#include "ui/browse_source.h"
#include "ui/browser_entry.h"
#include "ui/page_launches.h"
#include "ui/script_kind.h"
#include "ui/script_launch.h"

namespace mister::ui {

void ScriptPickHost::bind(Borrows b) noexcept {
    TASTY_SEAT_BODY(ScriptPickHost);
    b_ = b;
}

Transition ScriptPickHost::on_cancel() {
    TASTY_SEAT_BODY(ScriptPickHost);
    return Transition{Transition::Kind::Pop};
}

Transition ScriptPickHost::on_picked(const BrowserEntry&, std::string_view path) {
    TASTY_SEAT_BODY(ScriptPickHost);

    const infra::OptRef<ScriptLaunch> script = b_.launches ? b_.launches->script
                                                           : infra::OptRef<ScriptLaunch>{};
    if (!script || !b_.storage || path.empty()) {
        ++refusals_;
        return Transition{Transition::Kind::Pop};
    }
    ScriptLaunch::Request req;
    req.kind = ScriptKind::Script;
    req.argv0 = std::string(b_.storage->root_path()) + "/" + std::string(path);
    script->pending = static_cast<ScriptLaunch::Request&&>(req);
    return Transition{Transition::Kind::Replace, PageId::ScriptRun};
}

}
