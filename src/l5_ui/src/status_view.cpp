// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/status_view.h"

namespace mister::ui {

proto::FieldValue StatusView::field(BitField b) const noexcept {
    return status.field(b);
}

}
