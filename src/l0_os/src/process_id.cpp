// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/process_id.h"

#include <signal.h>
#include <unistd.h>

#include <cerrno>

namespace mister::os {

ProcessId ProcessId::self() noexcept {
    return ProcessId{::getpid()};
}

bool ProcessId::is_self() const noexcept {
    return v_ == ::getpid();
}

ProcessId::Presence ProcessId::presence() const noexcept {
    if (::kill(v_, 0) == 0)
        return Presence::Alive;
    return errno == ESRCH ? Presence::Gone : Presence::Unreachable;
}

Ex<void> ProcessId::terminate() const noexcept {
    if (::kill(v_, SIGTERM) != 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    return {};
}

Ex<void> ProcessId::kill_now() const noexcept {
    if (::kill(v_, SIGKILL) != 0)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    return {};
}

}
