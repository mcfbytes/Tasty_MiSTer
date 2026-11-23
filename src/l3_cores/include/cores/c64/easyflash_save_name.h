// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

#include "infra/path_parts.h"
#include "infra/path_text.h"
#include "infra/prelude.h"

namespace mister::cores::c64 {

inline constexpr std::string_view kCartExt = ".CRT";
inline constexpr std::string_view kSaveExt = ".sav";

[[nodiscard]] constexpr std::optional<infra::PathText> save_name_of(
    std::string_view pick
) noexcept {
    infra::PathText name{};
    if (!name.assign(infra::stem(pick)) || !name.append(kSaveExt))
        return std::nullopt;
    return name;
}

}
