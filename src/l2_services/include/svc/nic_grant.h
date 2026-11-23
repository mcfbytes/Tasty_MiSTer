// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "svc/nic_link.h"
#include "svc/nic_port.h"
#include "svc/nic_window.h"

namespace mister::svc {

struct NicGrant {
    INicWindow& window;
    INicPort& port;
    NicLink link = NicLink::Off;
};

}
