// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace mister::cores {

inline constexpr std::string_view kGamesDir = "games/";

struct ShareRootRule {
    std::string_view home;
    std::string_view configured;
    std::string_view default_dir;
    std::string_view home_name{};
};

[[nodiscard]] inline std::string share_root(const ShareRootRule& rule) {
    std::string root;
    if (!rule.configured.empty() && rule.configured.front() == '/') {
        root.assign(rule.configured);
    } else {
        if (rule.home.empty() && !rule.home_name.empty()) {
            root.assign(kGamesDir);
            root.append(rule.home_name);
        } else {
            root.assign(rule.home);
        }
        root.push_back('/');
        root.append(rule.configured.empty() ? rule.default_dir : rule.configured);
    }
    if (!root.empty() && root.back() == '/')
        root.pop_back();
    return root;
}

}
