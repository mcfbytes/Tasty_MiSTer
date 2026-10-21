// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstring>
#include <string_view>

#include "infra/fixed_str.h"

namespace mister::app {

inline constexpr std::size_t kConfigRelCap = 256;

inline FixedStr<kConfigRelCap, StrFit::Reject> config_rel(
    std::string_view dir,
    std::string_view name
) {
    FixedStr<kConfigRelCap, StrFit::Reject> rel;
    char buf[kConfigRelCap];
    const std::size_t n = dir.size() + name.size();
    if (name.empty() || n > decltype(rel)::kCapacity)
        return rel;
    std::memcpy(buf, dir.data(), dir.size());
    std::memcpy(buf + dir.size(), name.data(), name.size());
    if (!rel.assign(std::string_view{buf, n}))
        rel.clear();
    return rel;
}

}
