// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/timer_slack.h"

#include <sys/prctl.h>

#include <cerrno>

namespace mister::os {
namespace {

using PrctlNanos = std::chrono::duration<unsigned long, std::nano>;

[[nodiscard]] Ex<void> set_nanoseconds(PrctlNanos ns) noexcept {
    if (::prctl(PR_SET_TIMERSLACK, ns.count(), 0ul, 0ul, 0ul) != 0) {
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }
    return {};
}

}

[[nodiscard]] Ex<void> set_timer_slack(std::chrono::nanoseconds slack) noexcept {
    return set_nanoseconds(PrctlNanos{slack});
}

[[nodiscard]] Ex<void> restore_default_timer_slack() noexcept {
    return set_nanoseconds(PrctlNanos::zero());
}

}
