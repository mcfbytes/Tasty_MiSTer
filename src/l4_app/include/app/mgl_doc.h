// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/mgl.h"
#include "app/path_text.h"
#include "infra/fixed_str.h"

namespace mister::app {

struct Mgl final {
    FixedStr<kPathMax, StrFit::Clip> rbf{};
    FixedStr<kPathMax, StrFit::Clip> path{};
    MglItem::Slot slot = MglItem::Slot::File;
    proto::FileSlotDigit index{};
    DelaySeconds delay_s{};

    [[nodiscard]] bool complete() const noexcept {
        return !rbf.view().empty() && !path.view().empty();
    }
};

}
