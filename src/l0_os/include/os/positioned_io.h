// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/byte_count.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/unique_fd.h"
#include "os/types.h"

namespace mister::os {

enum class Advice : std::uint8_t { Sequential, WillNeed };

[[nodiscard]] Ex<infra::ByteCount> pread_at(
    const UniqueFd& fd,
    std::span<std::byte> dst,
    SeekOffset at
) noexcept;

[[nodiscard]] Ex<infra::ByteCount> pwrite_at(
    const UniqueFd& fd,
    std::span<const std::byte> src,
    SeekOffset at
) noexcept;

[[nodiscard]] Ex<void> advise(
    const UniqueFd& fd,
    SeekOffset at,
    SeekLength len,
    Advice advice
) noexcept;

}
