// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "infra/error.h"
#include "infra/unique_fd.h"

namespace mister::os {

[[nodiscard]] Ex<void> write_all(const UniqueFd& fd, std::span<const std::byte> bytes) noexcept;

[[nodiscard]] Ex<void> rewrite_head(const UniqueFd& fd, std::span<const std::byte> bytes) noexcept;

}
