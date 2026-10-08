// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_registry.h"

#include <array>
#include <cctype>
#include <cstddef>
#include <string_view>

#include "cores/generic_core.h"
#include "cores/registry.h"
#include "cores/movie_system.h"
#include "cores/manifests/megadrive.h"
#include "cores/manifests/psx.h"
#include "cores/manifests/snes.h"

namespace mister::fw {
namespace {

const auto kTastyCores = std::to_array<cores::CoreFactory>({
    {.kind = cores::CoreKind::Menu,
     .name = "MENU",
     .profile = &cores::kMenuProfile,
     .make = &cores::make_generic},
    {.kind = cores::CoreKind::Snes,
     .name = "SNES",
     .profile = &cores::manifests::kSnes,
     .make = &cores::manifests::make_snes,
     .make_companion = &cores::manifests::make_snes_companion},
    {.kind = cores::CoreKind::MegaDrive,
     .name = "MegaDrive",
     .profile = &cores::manifests::kMegaDrive,
     .make = &cores::manifests::make_megadrive},
    {.kind = cores::CoreKind::Psx,
     .name = "PSX",
     .profile = &cores::manifests::kPsx,
     .make = &cores::manifests::make_psx,
     .make_ladder = &cores::manifests::make_psx_ladder},
});

bool ieq(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb)) return false;
    }
    return true;
}

}  // namespace

bool tasty_plays(std::string_view conf_str_name) noexcept {
    if (conf_str_name.empty()) return false;
    if (cores::movie_system_plays(conf_str_name)) return true;
    for (const cores::CoreFactory& f : kTastyCores) {
        if (ieq(f.name, conf_str_name)) return true;
    }
    return false;
}

}  // namespace mister::fw
