// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

#include "app/hd/page_description.h"
#include "infra/fixed_str.h"
#include "svc/connect/bt_failure.h"
#include "svc/connect/bt_view.h"
#include "svc/connect/wifi_failure.h"
#include "svc/connect/wifi_scan_view.h"
#include "svc/connect/wifi_view.h"

namespace mister::ui {

using ConnectCell = decltype(app::hd::PageDescription::Row::value);

[[nodiscard]] app::hd::PageDescription::Row connect_row(
    app::hd::PageDescription::RowKind kind,
    std::string_view label,
    std::string_view value = {}
);

[[nodiscard]] ConnectCell wifi_cell(const std::optional<svc::connect::WifiView>& view);
[[nodiscard]] ConnectCell bt_cell(const std::optional<svc::connect::BtView>& view);

[[nodiscard]] ConnectCell scan_cell(const svc::connect::WifiScanViewRow& row);
[[nodiscard]] std::string_view wifi_failure_text(svc::connect::WifiFailure failure);
[[nodiscard]] std::string_view bt_failure_text(svc::connect::BtFailure failure);

}
