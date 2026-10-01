// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace mister::fw {

[[nodiscard]] bool tasty_plays(std::string_view conf_str_name) noexcept;

}
