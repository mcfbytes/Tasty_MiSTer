// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/play_refusal.h"

#include <array>
#include <cstddef>

namespace mister::app {
namespace {

constexpr std::array<const char*, static_cast<std::size_t>(PlayRefusal::Why::kCount)> kNames{
    "none",
    "busy",
    "not_wired",
    "no_codec",
    "movie_io",
    "movie",
    "rom_io",
    "rom_size",
    "checksum",
    "setting",
    "phase",
    "lead",
    "command",
    "too_long",
    "slot",
    "power_on",
    "firmware",
    "rom_kind",
    "rom_chd"
};
static_assert(kNames.back() != nullptr, "every Refusal has a name");

}

const char* PlayRefusal::name(Why r) noexcept {
    const auto i = static_cast<std::size_t>(r);
    return i < kNames.size() ? kNames[i] : "?";
}

}
