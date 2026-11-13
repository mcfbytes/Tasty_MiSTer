// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/mgl_delay.h"
#include "app/mgl_item.h"
#include "app/path_text.h"
#include "infra/fixed_str.h"

namespace mister::app {

struct Mgl final {
    FixedStr<kPathMax, StrFit::Clip> rbf{};
    FixedStr<kPathMax, StrFit::Clip> path{};
    proto::FileRowKind slot = proto::FileRowKind::File;
    proto::FileSlotDigit index{};
    DelaySeconds delay_s{};

    [[nodiscard]] bool complete() const noexcept {
        return !rbf.view().empty() && !path.view().empty();
    }
};

}
