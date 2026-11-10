// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_registry.h"

#include <string_view>

#include "cores/movie_system.h"
#include "cores/registry.h"
#include "infra/ascii.h"

namespace mister::fw {

bool tasty_plays(std::string_view conf_str_name) noexcept {
    if (conf_str_name.empty())
        return false;
    if (cores::movie_system_plays(conf_str_name))
        return true;
    for (const cores::CoreFactory& f : cores::core_table()) {
        if (f.profile.tasty_playable && infra::ieq(f.profile.name, conf_str_name))
            return true;
    }
    return false;
}

}
