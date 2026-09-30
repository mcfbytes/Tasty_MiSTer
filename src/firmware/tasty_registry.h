// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string_view>

#include "cores/registry.h"

namespace mister::fw {

[[nodiscard]] std::span<const cores::CoreFactory> tasty_core_table() noexcept;
[[nodiscard]] bool tasty_plays(std::string_view conf_str_name) noexcept;

}  // namespace mister::fw
