// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdio>
#include <string>
#include <string_view>

#include "app/game_id.h"
#include "proto/rotation_dir.h"

namespace mister::app {

struct MraFacts {

    bool is_arcade = false;
    bool vertical = false;

    proto::RotationDir rotation = proto::RotationDir::None;
    std::string setname;
    bool setname_same_dir = false;
};

[[nodiscard]] inline std::string_view effective_name(std::string_view conf_str_name,
                                                     const MraFacts& facts) noexcept {
    return facts.setname.empty() ? conf_str_name : std::string_view(facts.setname);
}

[[nodiscard]] inline std::string default_manifest_rel(std::string_view text) {
    if (text.empty()) return {};
    if (text.front() == '/') return std::string(text.substr(1));
    std::string rel = "_Arcades/";
    rel.append(text);
    return rel;
}

[[nodiscard]] inline GameId game_id_of(const MraFacts& facts, bool enabled) noexcept {
    GameId out{};
    if (facts.setname.empty()) return out;
    (void)out.serial.assign(facts.setname);
    char name[GameId::kNameCap] = {};
    (void)std::snprintf(name, sizeof name, "%s.mra", out.serial.c_str());
    (void)out.file.assign(name);
    out.enabled = enabled;
    return out;
}

}  // namespace mister::app
