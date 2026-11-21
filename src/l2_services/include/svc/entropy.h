// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "infra/error.h"
#include "infra/prelude.h"

namespace mister::svc {

[[nodiscard]] Ex<void> read_entropy(std::span<std::uint8_t> out) noexcept;

}
