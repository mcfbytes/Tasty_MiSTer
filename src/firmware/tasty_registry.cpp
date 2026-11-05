// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_registry.h"

#include <cctype>
#include <cstddef>
#include <string_view>

#include "cores/movie_system.h"
#include "cores/registry.h"

namespace mister::fw {
namespace {

bool ieq(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
            return false;
    }
    return true;
}

}

bool tasty_plays(std::string_view conf_str_name) noexcept {
    if (conf_str_name.empty())
        return false;
    if (cores::movie_system_plays(conf_str_name))
        return true;
    for (const cores::CoreFactory& f : cores::core_table()) {
        if (f.profile->tasty_playable && ieq(f.name, conf_str_name))
            return true;
    }
    return false;
}

}
