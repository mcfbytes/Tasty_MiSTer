// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <signal.h>
#include <spawn.h>

#include <array>
#include <cstdint>
#include <expected>

#include "infra/error.h"

namespace mister::os::detail {

inline constexpr std::array<int, 5> kStopSignals{SIGINT, SIGTERM, SIGQUIT, SIGHUP, SIGPIPE};

enum class ChildSignalReset : std::uint8_t { stop_set, every_signal };

[[nodiscard]] inline Ex<void> set_child_signal_state(
    posix_spawnattr_t& attr,
    ChildSignalReset reset,
    bool new_session
) noexcept {
    sigset_t none;
    sigset_t dfl;
    ::sigemptyset(&none);
    if (reset == ChildSignalReset::every_signal) {
        ::sigfillset(&dfl);
    } else {
        ::sigemptyset(&dfl);
        for (const int sig : kStopSignals)
            (void)::sigaddset(&dfl, sig);
    }
    if (const int rc = ::posix_spawnattr_setsigmask(&attr, &none))
        return std::unexpected(infra::errno_error(Errc::io, ERR_SITE(), rc));
    if (const int rc = ::posix_spawnattr_setsigdefault(&attr, &dfl))
        return std::unexpected(infra::errno_error(Errc::io, ERR_SITE(), rc));
    const short flags = new_session
        ? POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF
        : POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
    if (const int rc = ::posix_spawnattr_setflags(&attr, flags))
        return std::unexpected(infra::errno_error(Errc::io, ERR_SITE(), rc));
    return {};
}

}
