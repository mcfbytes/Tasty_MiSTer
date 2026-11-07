// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "hal/types.h"

namespace mister::hal {

inline constexpr CoreTypeByte kUnnegotiatedType{0x55};

struct CoreIdentity {
    CoreTypeByte type{kUnnegotiatedType};

    [[nodiscard]] constexpr bool dual_sdram() const noexcept { return type.dual_sdram(); }
};

}
