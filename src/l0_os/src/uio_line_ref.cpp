// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/uio_line_ref.h"

#include <unistd.h>

#include <cerrno>
#include <cstdint>

namespace mister::os {

Ex<std::uint32_t> UioLineRef::consume() const {

    std::uint32_t count = 0;
    const ssize_t n = ::read(fd_, &count, sizeof count);
    if (n < 0) {
        const int e = errno;

        return std::unexpected(
            infra::errno_error(e == EAGAIN ? Errc::would_block : Errc::io, ERR_SITE(), e)
        );
    }
    if (n != static_cast<ssize_t>(sizeof count)) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(n)});
    }
    return count;
}

Ex<void> UioLineRef::rearm() const {
    const std::uint32_t enable = 1;
    const ssize_t w = ::write(fd_, &enable, sizeof enable);
    if (w < 0) {
        return std::unexpected(infra::errno_error(Errc::io, ERR_SITE(), errno));
    }
    if (w != static_cast<ssize_t>(sizeof enable)) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(w)});
    }
    return {};
}

}
