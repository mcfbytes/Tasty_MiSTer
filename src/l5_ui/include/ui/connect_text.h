// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

#include "app/page_description.h"
#include "infra/fixed_str.h"
#include "svc/bt_failure.h"
#include "svc/bt_view.h"
#include "svc/wifi_failure.h"
#include "svc/wifi_scan_view.h"
#include "svc/wifi_view.h"

namespace mister::ui {

using ConnectCell = decltype(app::PageDescription::Row::value);

[[nodiscard]] app::PageDescription::Row connect_row(
    app::PageDescription::RowKind kind,
    std::string_view label,
    std::string_view value = {}
);

[[nodiscard]] ConnectCell wifi_cell(const std::optional<svc::WifiView>& view);
[[nodiscard]] ConnectCell bt_cell(const std::optional<svc::BtView>& view);

[[nodiscard]] ConnectCell scan_cell(const svc::WifiScanViewRow& row);
[[nodiscard]] std::string_view wifi_failure_text(svc::WifiFailure failure);
[[nodiscard]] std::string_view bt_failure_text(svc::BtFailure failure);

}
