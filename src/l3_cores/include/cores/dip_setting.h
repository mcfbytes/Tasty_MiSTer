// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "cores/types.h"

namespace mister::cores {

struct DipSetting {
    mra::DipWord mask{};
    mra::DipWord val{};
    friend constexpr bool operator==(DipSetting, DipSetting) = default;
};

constexpr mra::DipWord mra::DipWord::with(DipSetting setting) const noexcept {
    return mra::DipWord{(v & ~setting.mask.v) | setting.val.v};
}

}
