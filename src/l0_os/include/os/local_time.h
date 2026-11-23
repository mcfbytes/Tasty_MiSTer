// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <ctime>
#include <optional>

namespace mister::os {

[[nodiscard]] std::optional<std::tm> local_now() noexcept;

}
