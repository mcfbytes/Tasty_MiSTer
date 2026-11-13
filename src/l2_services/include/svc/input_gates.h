// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::svc {

struct InputGates {
    bool grabbed = false;
    bool osd_visible = false;
    bool keys_lost = false;
    friend constexpr bool operator==(InputGates, InputGates) = default;
};

}
