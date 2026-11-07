// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ui/page_id.h"
#include "ui/types.h"

namespace mister::ui {

struct FocusKey {
    PageId page = PageId::None;
    ItemId item{};
    friend constexpr bool operator==(FocusKey, FocusKey) = default;
};

}
