// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/seat.h"
#include "infra/telemetry.h"
#include "svc/bt_device_view.h"
#include "svc/bt_prompt_view.h"
#include "svc/bt_view.h"
#include "svc/wifi_saved_view.h"
#include "svc/wifi_scan_view.h"
#include "svc/wifi_view.h"

namespace mister::app {

using WifiViewCell = xthread::Telemetry<svc::WifiView, SeatTag::Connect>;
using WifiScanViewCell = xthread::Telemetry<svc::WifiScanView, SeatTag::Connect>;
using WifiSavedViewCell = xthread::Telemetry<svc::WifiSavedView, SeatTag::Connect>;
using BtViewCell = xthread::Telemetry<svc::BtView, SeatTag::Connect>;
using BtDeviceViewCell = xthread::Telemetry<svc::BtDeviceView, SeatTag::Connect>;
using BtPromptViewCell = xthread::Telemetry<svc::BtPromptView, SeatTag::Connect>;

}
