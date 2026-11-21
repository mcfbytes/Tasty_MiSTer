// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <fcntl.h>

#include <cstdint>
#include <expected>

#include "infra/errno.h"
#include "infra/file_mode.h"
#include "infra/unique_fd.h"

namespace mister::os {

class OpenFlags {
public:
    [[nodiscard]] static constexpr OpenFlags read_only() noexcept { return OpenFlags{O_RDONLY}; }

    [[nodiscard]] static constexpr OpenFlags write_only() noexcept { return OpenFlags{O_WRONLY}; }

    [[nodiscard]] static constexpr OpenFlags read_write() noexcept { return OpenFlags{O_RDWR}; }

    [[nodiscard]] constexpr OpenFlags creating() const noexcept { return OpenFlags{v | O_CREAT}; }

    [[nodiscard]] constexpr OpenFlags truncating() const noexcept { return OpenFlags{v | O_TRUNC}; }

    [[nodiscard]] constexpr OpenFlags exclusive() const noexcept { return OpenFlags{v | O_EXCL}; }

    [[nodiscard]] constexpr OpenFlags synced() const noexcept { return OpenFlags{v | O_SYNC}; }

    [[nodiscard]] constexpr int open_word() const noexcept { return v | O_CLOEXEC; }

    friend constexpr bool operator==(OpenFlags, OpenFlags) = default;

private:
    constexpr explicit OpenFlags(int bits) noexcept : v(bits) {}

    int v;
};

[[nodiscard]] std::expected<infra::UniqueFd, infra::Errno> open_existing(
    const char* path,
    OpenFlags flags
) noexcept;

[[nodiscard]] std::expected<infra::UniqueFd, infra::Errno> open_created(
    const char* path,
    OpenFlags flags,
    infra::FileMode mode
) noexcept;

enum class DirMade : std::uint8_t { Created, Existed };

[[nodiscard]] std::expected<DirMade, infra::Errno> make_dir(
    const char* path,
    infra::FileMode mode
) noexcept;

}
