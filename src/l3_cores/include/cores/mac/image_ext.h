// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "infra/ascii.h"

namespace mister::cores::mac {

struct ImageExt {
    std::string_view dotted;

    [[nodiscard]] constexpr bool names(std::string_view name) const noexcept {
        return name.size() > dotted.size() && infra::iends_with(name, dotted);
    }
};

inline constexpr ImageExt kIsoExt{".iso"};
inline constexpr ImageExt kToastExt{".toast"};
inline constexpr ImageExt kToaExt{".toa"};
inline constexpr ImageExt kCueExt{".cue"};
inline constexpr ImageExt kChdExt{".chd"};
inline constexpr ImageExt kBinExt{".bin"};
static_assert(kCueExt.names("Disc.CUE") && !kCueExt.names(".cue"));

}
