// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/key_focus.h"
#include "app/launcher_key_bridge.h"
#include "app/raw_key_edge.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "svc/device_report.h"
#include "svc/types.h"

namespace mister::app {

class InputDecode;

using MenuEdgeSink = bool (*)(InputDecode& lane, const RawKeyEdge& edge);

class JoyMenuChords {
    TASTY_SEAT_RESIDENT(Input);

public:
    explicit JoyMenuChords(infra::OptRef<LauncherKeyBridge> launcher_keys) noexcept
        : launcher_keys_(launcher_keys) {}

    JoyMenuChords(const JoyMenuChords&) = delete;
    JoyMenuChords& operator=(const JoyMenuChords&) = delete;

    void publish_joy_menu(
        const svc::DeviceReport& r,
        svc::DeviceSlot slot,
        KeyFocus focus,
        bool suppressed,
        svc::JoyMask& prev_menu,
        InputDecode& lane,
        MenuEdgeSink sink
    );

private:
    infra::OptRef<LauncherKeyBridge> launcher_keys_;

    svc::JoyMask osdbtn_{};
};

}
