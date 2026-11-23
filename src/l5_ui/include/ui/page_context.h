// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/ini_cell.h"
#include "app/link_tx_channel.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/wake_flag.h"
#include "proto/osd_rotation.h"
#include "ui/page_launches.h"
#include "ui/status_bar_facts.h"
#include "ui/system_facts.h"

namespace mister::app {
class ICmdVerbSink;
class IdentityLatch;
class InfoSink;
class CheatStore;
class NameConfig;
class UiRequestRing;
}

namespace mister::app::recents {
class PlayedHistory;
class RecentsStore;
class UiWriteLane;
}

namespace mister::app::input {
struct JoyMapEnds;
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
class FbTerminalHost;
class KeyDecoder;

struct PageContext {
    TASTY_SEAT_RESIDENT(Ui);

    infra::OptRef<app::LinkTxChannel<SeatTag::Ui>> link_tx{};
    infra::OptRef<app::InfoSink> info{};
    infra::OptRef<KeyDecoder> keys{};
    infra::OptRef<const os::IClock> clock{};

    infra::OptRef<const IBrowseSource> storage{};

    PageLaunches launches{};

    infra::OptRef<app::recents::RecentsStore> recents{};
    infra::OptRef<app::NameConfig> names{};

    infra::OptRef<app::recents::PlayedHistory> played{};

    infra::OptRef<app::CheatStore> cheats{};

    infra::OptRef<app::ICmdVerbSink> route{};

    infra::OptRef<app::UiRequestRing> ui_requests{};

    infra::OptRef<ConnectPort> connect{};

    infra::OptRef<const app::input::JoyMapEnds> joymap{};
    infra::OptRef<const svc::Vfs> files{};

    infra::OptRef<app::recents::UiWriteLane> writes{};
    infra::OptRef<const app::IdentityLatch> identity{};

    infra::OptRef<FbTerminalHost> terminal{};

    bool bt_reset_before_pair = false;

    proto::OsdRotation osd_rotation = proto::OsdRotation::None;

    SystemFacts sys{};
    StatusBarFacts status{};

    SessionFacts session{};

    xthread::WakeFlag& disk_activity;
    const app::IniCell& ini;
};

}
