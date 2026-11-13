// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/epoll_floor.h"

#include <sys/epoll.h>

#include <cerrno>
#include <ctime>

#include "infra/unique_fd.h"

namespace mister::os {
namespace {

constexpr timespec kImmediate{};

}

[[nodiscard]] Ex<void> check_epoll_pwait2() noexcept {
    UniqueFd ep{::epoll_create1(EPOLL_CLOEXEC)};
    if (!ep.valid()) {
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }
    epoll_event ev{};
    if (::epoll_pwait2(ep.get(), &ev, 1, &kImmediate, nullptr) < 0) {

        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }
    return {};
}

}
