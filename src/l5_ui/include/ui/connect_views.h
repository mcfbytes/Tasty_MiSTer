// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/seat.h"
#include "svc/connect/bt_device_view.h"
#include "svc/connect/bt_prompt_view.h"
#include "svc/connect/bt_view.h"
#include "svc/connect/wifi_saved_view.h"
#include "svc/connect/wifi_scan_view.h"
#include "svc/connect/wifi_view.h"

namespace mister::ui {

struct ConnectViews {
    TASTY_SEAT_RESIDENT(Ui);

    std::optional<svc::connect::WifiView> wifi{};
    std::optional<svc::connect::WifiScanView> scan{};
    std::optional<svc::connect::WifiSavedView> saved{};
    std::optional<svc::connect::BtView> bt{};
    std::optional<svc::connect::BtDeviceView> devices{};
    std::optional<svc::connect::BtPromptView> prompt{};
};

}
