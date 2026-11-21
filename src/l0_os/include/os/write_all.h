// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/unique_fd.h"

namespace mister::os {

[[nodiscard]] Ex<void> write_all(const UniqueFd& fd, std::span<const std::byte> bytes) noexcept;

[[nodiscard]] Ex<void> rewrite_head(const UniqueFd& fd, std::span<const std::byte> bytes) noexcept;

void write_all_best_effort(const UniqueFd& fd, std::span<const std::byte> bytes) noexcept;

inline void write_all_best_effort(const UniqueFd& fd, std::string_view text) noexcept {
    write_all_best_effort(fd, std::as_bytes(std::span(text)));
}

}
