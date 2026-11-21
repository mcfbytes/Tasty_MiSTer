// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <initializer_list>
#include <string>
#include <string_view>

#include "infra/prelude.h"
#include "proto/save_state_name.h"

namespace mister::svc {

inline constexpr std::string_view kSdRoot = "/media/fat";

inline constexpr std::string_view kConfigDir = "config";
inline constexpr std::string_view kSavesDir = "saves";

inline constexpr std::string_view kSaveStateDir = proto::kSaveStateRoot;

inline constexpr std::string_view kSavesPrefix = "saves/";
inline constexpr std::string_view kSdSavesPrefix = "/media/fat/saves/";

[[nodiscard]] inline std::string card_rel(std::initializer_list<std::string_view> parts) {
    std::string out;
    for (const std::string_view part : parts) {
        if (part.empty())
            continue;
        if (!out.empty())
            out.push_back('/');
        out.append(part);
    }
    return out;
}

}
