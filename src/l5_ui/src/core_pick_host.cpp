// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/core_pick_host.h"

#include "app/cmd_verb_sink.h"
#include "app/xml_kind.h"
#include "app/path_text.h"
#include "app/recents_store.h"
#include "ui/browser_entry.h"

namespace mister::ui {

void CorePickHost::bind(Borrows b) noexcept {
    TASTY_SEAT_BODY(CorePickHost);
    b_ = b;
}

Transition CorePickHost::on_cancel() {
    TASTY_SEAT_BODY(CorePickHost);
    return Transition::pop();
}

bool CorePickHost::deliver_pick_(std::string_view path) {
    if (!b_.route)
        return false;
    switch (app::xml_kind_of(path)) {
        case app::XmlKind::Mgl:
            return b_.route->on(app::CmdVerb::Playlist{path});
        case app::XmlKind::Mra:
            return b_.route->on(app::CmdVerb::LoadCore{path, app::CmdVerb::RbfOrMra::Mra});
        case app::XmlKind::Rbf:
            return b_.route->on(app::CmdVerb::LoadCore{path, app::CmdVerb::RbfOrMra::Rbf});
    }
    return false;
}

Transition CorePickHost::on_picked(const BrowserEntry& e, std::string_view path) {
    TASTY_SEAT_BODY(CorePickHost);
    if (path.empty()) {
        refusals_.add();
        return Transition::close_all();
    }
    const app::RecentsStore::ListId cores_list = app::RecentsStore::ListId::cores();
    if (b_.recents
        && !b_.recents->update(app::RecentsStore::kNoCore, cores_list, path, e.altname)) {
        refusals_.add();
    }

    if (path.size() > app::PathText::kCapacity) {
        refusals_.add();
        return Transition::close_all();
    }
    if (deliver_pick_(path)) {
        ++published_;
    } else {
        refusals_.add();
    }
    return Transition::close_all();
}

}
