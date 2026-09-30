// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/small_file.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>

#include "infra/unique_fd.h"

namespace mister::os {

[[nodiscard]] Ex<std::size_t> read_small_file(const char* path, std::span<char> buf) noexcept {
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    UniqueFd guard{fd};

    std::size_t got = 0;
    while (got < buf.size()) {
        const ssize_t n = ::read(guard.get(), buf.data() + got, buf.size() - got);
        if (n < 0) {
            if (errno == EINTR) continue;
            return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
        }
        if (n == 0) break;
        got += static_cast<std::size_t>(n);
    }
    return got;
}

}  // namespace mister::os
