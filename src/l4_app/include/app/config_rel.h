// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "infra/fixed_str.h"
#include "infra/path_text.h"

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

inline constexpr std::string_view kCoreCfgDir = "config/";
inline constexpr std::string_view kCoreCfgExt = ".CFG";
using CoreCfgRel = FixedStr<
    kCoreCfgDir.size() + infra::PathText::kCapacity + kCoreCfgExt.size() + 1,
    StrFit::Reject>;
static_assert(
    CoreCfgRel::kCapacity == kCoreCfgDir.size() + infra::PathText::kCapacity + kCoreCfgExt.size()
);

[[nodiscard]] inline std::optional<CoreCfgRel> core_cfg_rel(std::string_view stem) noexcept {
    CoreCfgRel rel;
    if (stem.empty() || !rel.assign(kCoreCfgDir) || !rel.append(stem) || !rel.append(kCoreCfgExt))
        return std::nullopt;
    return rel;
}

}
