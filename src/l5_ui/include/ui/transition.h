// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "ui/page_id.h"
#include "ui/types.h"

namespace mister::ui {

struct Transition {
    enum class Kind : std::uint8_t {
        Stay,
        Push,
        Pop,
        Replace,
        CloseAll,
        Leave,
    };
    Kind kind = Kind::Stay;
    PageId page = PageId::None;
    ItemId focus = kNoItem;
};

}
