// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string_view>

#include "infra/fixed_str.h"

namespace mister::app {

inline constexpr std::size_t kConfigRelCap = 256;

inline FixedStr<kConfigRelCap, StrFit::Reject> config_rel(
    std::string_view dir,
    std::string_view name
) {
    FixedStr<kConfigRelCap, StrFit::Reject> rel;
    if (name.empty() || !rel.assign(dir))
        return rel;
    if (!rel.append(name))
        rel.clear();
    return rel;
}

}
