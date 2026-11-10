// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "proto/save_target.h"
#include "proto/state_slot.h"

namespace mister::proto {

inline constexpr std::string_view kSaveStateRoot = "savestates";
inline constexpr std::string_view kSaveStateArcadeSubdir = "Arcade";
inline constexpr std::string_view kSaveStateExt = ".ss";

[[nodiscard]] std::string savestate_stem(std::string_view rom_path);

struct SaveStateName {
    std::string subdir;
    std::string stem;

    [[nodiscard]] static std::optional<SaveStateName> of(const SaveTarget& target);

    [[nodiscard]] std::string dir() const;

    [[nodiscard]] std::string path(StateSlot slot) const;

    [[nodiscard]] std::string stock_path() const;
};

}
