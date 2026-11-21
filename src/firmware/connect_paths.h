// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "infra/seat.h"
#include "os/connect/dbus_connection.h"
#include "svc/connect/wifi_service.h"

namespace mister::fw {

struct ConnectPaths {
    TASTY_SEAT_EXEMPT(component);

    static constexpr std::string_view kWpaConf{"/media/fat/linux/wpa_supplicant.conf"};

    std::string_view conf = kWpaConf;
    std::string_view net_class = svc::connect::kNetClassRoot;
    std::string_view bus_socket = os::connect::DbusConnection::kSystemBusSocket;
};

}
