// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "infra/error.h"
#include "infra/path_text.h"
#include "os/process_name.h"

namespace mister::fw {

inline constexpr const char* kTastyLockPath = "/tmp/tasty.lock";

inline constexpr const char* kTastySaveRoot = "tasty/saves";
inline constexpr const char* kTastyPidPath = "/tmp/tasty.pid";
inline constexpr const char* kTastyStatusPath = "/tmp/tasty.status";
inline constexpr const char* kMisterCmdPath = "/dev/MiSTer_cmd";
inline constexpr const char* kStockMisterPath = "/media/fat/MiSTer";
inline constexpr const char* kMenuRbfName = "menu.rbf";

inline constexpr os::ProcessName kOwnerName = *os::ProcessName::of("tasty");
inline constexpr os::ProcessName kStockName = *os::ProcessName::of("MiSTer");

struct TastyPaths {
    infra::PathText lock{};
    infra::PathText pid{};
    infra::PathText status{};
    infra::PathText cmd{};

    TastyPaths() noexcept;

    [[nodiscard]] static Ex<TastyPaths> in(std::string_view dir) noexcept;

    [[nodiscard]] Ex<infra::PathText> status_tmp() const noexcept;
};

}
