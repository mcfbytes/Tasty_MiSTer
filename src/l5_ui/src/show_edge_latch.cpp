// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/show_edge_latch.h"

namespace mister::ui {

bool ShowEdgeLatch::on_open(bool open_now) noexcept {
    TASTY_SEAT_BODY(ShowEdgeLatch);
    if (open_now == was_open_)
        return false;
    was_open_ = open_now;
    if (!open_now)
        return false;
    ++edges_;
    return true;
}

}
