// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/status_bar_facts.h"
#include "ui/system_facts.h"

namespace mister::ui {

struct HostSample {
    SystemFacts sys{};
    StatusBarFacts bar{};
};

}
