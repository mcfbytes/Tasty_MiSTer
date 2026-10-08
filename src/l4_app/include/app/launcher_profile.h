// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cctype>
#include <cstddef>
#include <string_view>

#include "infra/seat.h"

namespace mister::app {

struct LauncherProfile {
    TASTY_SEAT_EXEMPT(const_shared);

    std::string_view main_value;
    std::string_view program;
    std::string_view front_end_image;
    std::string_view scanout_device;
    std::string_view tty;
    int vt = 0;
};

inline constexpr std::array<LauncherProfile, 1> kLaunchers{{
    {.main_value = "zaparoo/MiSTer_Zaparoo",
     .program = "zaparoo/frontend",
     .front_end_image = "zaparoo/menu_zaparoo.rbf",
     .scanout_device = "/dev/zaparoo-scanout",
     .tty = "/dev/tty7",
     .vt = 7},
}};

namespace detail {

constexpr bool ieq(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        const auto la = (ca >= 'A' && ca <= 'Z') ? ca + 32 : ca;
        const auto lb = (cb >= 'A' && cb <= 'Z') ? cb + 32 : cb;
        if (la != lb) return false;
    }
    return true;
}

}  // namespace detail

constexpr const LauncherProfile* launcher_for_main(std::string_view cfg_main) noexcept {
    for (const LauncherProfile& p : kLaunchers) {
        if (detail::ieq(cfg_main, p.main_value)) return &p;
        if (cfg_main.size() > p.main_value.size() && cfg_main.front() == '/' &&
            cfg_main[cfg_main.size() - p.main_value.size() - 1] == '/' &&
            detail::ieq(cfg_main.substr(cfg_main.size() - p.main_value.size()), p.main_value))
            return &p;
    }
    return nullptr;
}

static_assert(launcher_for_main("zaparoo/MiSTer_Zaparoo") == &kLaunchers[0]);
static_assert(launcher_for_main("/media/fat/zaparoo/mister_zaparoo") == &kLaunchers[0]);
static_assert(launcher_for_main("MiSTer") == nullptr);
static_assert(launcher_for_main("xzaparoo/MiSTer_Zaparoo") == nullptr);
static_assert(launcher_for_main("") == nullptr);
static_assert(launcher_for_main("backup/zaparoo/MiSTer_Zaparoo") == nullptr);

}  // namespace mister::app
