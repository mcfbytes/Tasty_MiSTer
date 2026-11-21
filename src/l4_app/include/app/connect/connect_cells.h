// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/telemetry.h"
#include "svc/connect/bt_device_view.h"
#include "svc/connect/bt_prompt_view.h"
#include "svc/connect/bt_view.h"
#include "svc/connect/wifi_saved_view.h"
#include "svc/connect/wifi_scan_view.h"
#include "svc/connect/wifi_view.h"

namespace mister::app::connect {

using WifiViewCell = xthread::Telemetry<svc::connect::WifiView, SeatTag::Connect>;
using WifiScanViewCell = xthread::Telemetry<svc::connect::WifiScanView, SeatTag::Connect>;
using WifiSavedViewCell = xthread::Telemetry<svc::connect::WifiSavedView, SeatTag::Connect>;
using BtViewCell = xthread::Telemetry<svc::connect::BtView, SeatTag::Connect>;
using BtDeviceViewCell = xthread::Telemetry<svc::connect::BtDeviceView, SeatTag::Connect>;
using BtPromptViewCell = xthread::Telemetry<svc::connect::BtPromptView, SeatTag::Connect>;

}
