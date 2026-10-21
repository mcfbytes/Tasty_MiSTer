// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/core_pick_host.h"

#include "app/cmd_verb_sink.h"
#include "app/xml_kind.h"
#include "app/path_text.h"
#include "app/recents_store.h"
#include "ui/browser_entry.h"

namespace mister::ui {

namespace {

app::XmlKind xml_kind_of_path(std::string_view s) {
    if (s.size() <= 4)
        return app::XmlKind::Rbf;
    const std::string_view tail = s.substr(s.size() - 4);
    auto ieq = [](std::string_view a, std::string_view b) {
        for (std::size_t i = 0; i < a.size(); ++i) {
            if ((a[i] | 0x20) != (b[i] | 0x20))
                return false;
        }
        return true;
    };
    if (ieq(tail, ".mra"))
        return app::XmlKind::Mra;
    if (ieq(tail, ".mgl"))
        return app::XmlKind::Mgl;
    return app::XmlKind::Rbf;
}

}

void CorePickHost::bind(Borrows b) noexcept {
    TASTY_SEAT_BODY(CorePickHost);
    b_ = b;
}

Transition CorePickHost::on_cancel() {
    TASTY_SEAT_BODY(CorePickHost);
    return Transition{Transition::Kind::Pop};
}

bool CorePickHost::deliver_pick_(std::string_view path) {
    if (!b_.route)
        return false;
    switch (xml_kind_of_path(path)) {
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
        ++refusals_;
        return Transition{Transition::Kind::CloseAll};
    }
    if (b_.recents
        && !b_.recents->update(
            "",
            app::RecentsStore::ListId{app::RecentsStore::ListId::Kind::Cores, 0},
            path,
            e.altname
        )) {
        ++refusals_;
    }

    if (path.size() > app::PathText::kCapacity) {
        ++refusals_;
        return Transition{Transition::Kind::CloseAll};
    }
    if (deliver_pick_(path)) {
        ++published_;
    } else {
        ++refusals_;
    }
    return Transition{Transition::Kind::CloseAll};
}

}
