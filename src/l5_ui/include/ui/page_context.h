// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/link_tx_channel.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/page_launches.h"
#include "ui/status_bar_facts.h"
#include "ui/system_facts.h"

namespace mister::app {
class ICmdVerbSink;
class IdentityLatch;
class InfoSink;
class InputWire;
class CheatStore;
class NameConfig;
class PlayedHistory;
class RecentsStore;
class UiRequestRing;
}

namespace mister::os {
class IClock;
}

namespace mister::svc {
class Vfs;
}

namespace mister::ui {

class IBrowseSource;
class IBtOps;
class KeyDecoder;

struct PageContext {
    TASTY_SEAT_RESIDENT(Ui);

    infra::OptRef<app::LinkTxChannel<SeatTag::Ui>> link_tx{};
    infra::OptRef<app::InfoSink> info{};
    infra::OptRef<KeyDecoder> keys{};
    infra::OptRef<const os::IClock> clock{};

    infra::OptRef<const IBrowseSource> storage{};

    PageLaunches launches{};

    infra::OptRef<app::RecentsStore> recents{};
    infra::OptRef<app::NameConfig> names{};

    infra::OptRef<app::PlayedHistory> played{};

    infra::OptRef<app::CheatStore> cheats{};

    infra::OptRef<app::ICmdVerbSink> route{};

    infra::OptRef<app::UiRequestRing> ui_requests{};

    infra::OptRef<IBtOps> bt{};

    infra::OptRef<app::InputWire> input{};
    infra::OptRef<const svc::Vfs> files{};
    infra::OptRef<const app::IdentityLatch> identity{};

    bool bt_reset_before_pair = false;
    SystemFacts sys{};
    StatusBarFacts status{};
};

}
