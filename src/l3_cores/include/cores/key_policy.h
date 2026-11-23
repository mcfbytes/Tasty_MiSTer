// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace mister::cores {

struct KeyPolicy {

    bool gui_chords_own = false;
    friend constexpr bool operator==(const KeyPolicy&, const KeyPolicy&) = default;
};

}
