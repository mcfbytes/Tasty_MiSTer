// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/error.h"
#include "infra/unique_fd.h"

namespace mister::os {

struct FileOffset {
    std::uint64_t v = 0;

    [[nodiscard]] constexpr off_t at(std::uint64_t done) const noexcept {
        return static_cast<off_t>(v + done);
    }

    friend constexpr bool operator==(FileOffset, FileOffset) = default;
};

static_assert(sizeof(FileOffset) == 8);

[[nodiscard]] Ex<void> write_all(const UniqueFd& fd, std::span<const std::byte> bytes) noexcept;

[[nodiscard]] Ex<void> rewrite_head(const UniqueFd& fd, std::span<const std::byte> bytes) noexcept;

[[nodiscard]] Ex<void> pwrite_all(
    const UniqueFd& fd,
    std::span<const std::byte> bytes,
    std::optional<FileOffset> offset
) noexcept;

}
