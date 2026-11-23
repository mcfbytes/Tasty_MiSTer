// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include "proto/types.h"

namespace mister::svc::t64 {

inline constexpr proto::FileSize kImageBytes{683 * 256};

inline constexpr proto::FileSize kMostTapeBytes{4 * 1024 * 1024};

[[nodiscard]] std::optional<std::vector<std::byte>> to_d64(std::span<const std::byte> tape);

}
