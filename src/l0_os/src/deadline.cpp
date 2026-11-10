// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/deadline.h"

#include "clock_bridge.h"

namespace mister::os {

Ex<void> Deadline::sleep_until() const {
    return detail::sleep_until_raw(at_);
}

}
