// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace mister::ui {

[[nodiscard]] std::string resolve_arc(std::string_view label, const std::string_view (&arc)[2]);

}
