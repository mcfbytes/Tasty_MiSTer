// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "svc/bt_device_view.h"
#include "svc/bt_prompt_view.h"
#include "svc/bt_view.h"
#include "svc/wifi_saved_view.h"
#include "svc/wifi_scan_view.h"
#include "svc/wifi_view.h"

namespace mister::ui {

struct ConnectViews {
    TASTY_SEAT_RESIDENT(Ui);

    std::optional<svc::WifiView> wifi{};
    std::optional<svc::WifiScanView> scan{};
    std::optional<svc::WifiSavedView> saved{};
    std::optional<svc::BtView> bt{};
    std::optional<svc::BtDeviceView> devices{};
    std::optional<svc::BtPromptView> prompt{};
};

}
