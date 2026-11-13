// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/ini_cell.h"
#include "app/link_tx_channel.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
#include "ui/page_launches.h"
#include "ui/status_bar_facts.h"
#include "ui/system_facts.h"

namespace mister::app {
class ICmdVerbSink;
class IdentityLatch;
class InfoSink;
struct JoyMapEnds;
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
class ConnectPort;
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

    infra::OptRef<ConnectPort> connect{};

    infra::OptRef<const app::JoyMapEnds> joymap{};
    infra::OptRef<const svc::Vfs> files{};
    infra::OptRef<const app::IdentityLatch> identity{};

    bool bt_reset_before_pair = false;

    SystemFacts sys{};
    StatusBarFacts status{};

    SessionFacts session{};

    xthread::WakeFlag& disk_activity;
    const app::IniCell& ini;
};

}
