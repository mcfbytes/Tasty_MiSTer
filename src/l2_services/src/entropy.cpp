// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/entropy.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

#include "infra/error.h"
#include "infra/unique_fd.h"

namespace mister::svc {

[[nodiscard]] Ex<void> read_entropy(std::span<std::uint8_t> out) noexcept {
    UniqueFd fd{::open("/dev/urandom", O_RDONLY | O_CLOEXEC)};
    if (!fd.valid())
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));

    auto n = ::read(fd.get(), out.data(), out.size());
    while (n < 0 && errno == EINTR)
        n = ::read(fd.get(), out.data(), out.size());
    if (n < 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    if (std::cmp_not_equal(n, out.size())) {
        return std::unexpected(Error{Errc::short_read, ERR_SITE(), 0});
    }
    return {};
}

}
