// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/unique_fd.h"

namespace mister::os {

enum class SyncKind : std::uint8_t { All, Data };

[[nodiscard]] Ex<void> sync(const UniqueFd& fd, SyncKind kind) noexcept;

}
