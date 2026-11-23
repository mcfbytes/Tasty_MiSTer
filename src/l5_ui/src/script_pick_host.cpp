// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/script_pick_host.h"

#include <string>

#include "ui/browse_source.h"
#include "ui/browser_entry.h"
#include "ui/page_launches.h"
#include "ui/script_launch.h"
#include "ui/session_facts.h"

namespace mister::ui {

void ScriptPickHost::bind(Borrows b) noexcept {
    TASTY_SEAT_BODY(ScriptPickHost);
    b_ = b;
}

Transition ScriptPickHost::on_cancel() {
    TASTY_SEAT_BODY(ScriptPickHost);
    return Transition::pop();
}

Transition ScriptPickHost::on_picked(const BrowserEntry&, std::string_view path) {
    TASTY_SEAT_BODY(ScriptPickHost);

    const infra::OptRef<ScriptLaunch> script = b_.launches ? b_.launches->script
                                                           : infra::OptRef<ScriptLaunch>{};
    if (!script || !b_.storage || path.empty()) {
        refusals_.add();
        return Transition::pop();
    }
    const std::string argv0 = std::string(b_.storage->root_path()) + "/" + std::string(path);
    const PageId page = b_.session && b_.session->console_view ? PageId::FbTerminal
                                                               : PageId::ScriptRun;
    return script->open(page, ScriptLaunch::Request::script(argv0), true);
}

}
