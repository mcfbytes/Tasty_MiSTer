// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_stock_control.h"

#include <dirent.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <optional>

#include "infra/parse_int.h"
#include "os/process_id.h"

namespace mister::fw {

namespace {

timespec to_timespec(std::chrono::steady_clock::duration d) noexcept {
    const auto whole = std::chrono::duration_cast<std::chrono::duration<std::time_t>>(d);
    const auto part = std::chrono::duration_cast<std::chrono::duration<long, std::nano>>(d - whole);
    timespec ts{};
    ts.tv_sec = whole.count();
    ts.tv_nsec = part.count();
    return ts;
}

void sleep_until(std::chrono::steady_clock::time_point deadline) noexcept {
    const timespec wake = to_timespec(deadline.time_since_epoch());
    while (::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wake, nullptr) == EINTR) {
    }
}

bool deadline_passed(std::chrono::steady_clock::time_point deadline) noexcept {
    return std::chrono::steady_clock::now() >= deadline;
}

void sleep_poll_slice(std::chrono::steady_clock::time_point deadline) noexcept {
    constexpr std::chrono::milliseconds kPollSlice{50};
    sleep_until(std::min(std::chrono::steady_clock::now() + kPollSlice, deadline));
}

void sleep_grace(std::chrono::seconds grace) noexcept {
    sleep_until(std::chrono::steady_clock::now() + grace);
}

}

Ex<bool> StockControl::stop() const noexcept {
    DIR* d = ::opendir("/proc");
    if (d == nullptr)
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    std::array<std::optional<os::ProcessId>, 16> pids{};
    std::size_t n = 0;
    while (const dirent* e = ::readdir(d)) {
        if (e->d_name[0] < '1' || e->d_name[0] > '9')
            continue;
        const auto raw = infra::parse_int<int>(e->d_name);
        if (!raw)
            continue;
        const auto pid = os::ProcessId::of(*raw);
        if (!pid || pid->is_self() || !stock_.names(*pid))
            continue;
        if (n < pids.size())
            pids[n++] = pid;
    }
    ::closedir(d);
    for (std::size_t i = 0; i < n; ++i)
        (void)pids[i]->terminate();
    constexpr std::chrono::seconds kTermGrace{2};
    constexpr std::chrono::seconds kKillGrace{3};
    if (n != 0)
        sleep_grace(kTermGrace);
    for (std::size_t i = 0; i < n; ++i) {
        if (pids[i]->presence() == os::ProcessId::Presence::Alive)
            (void)pids[i]->kill_now();
    }
    const auto deadline = std::chrono::steady_clock::now() + kKillGrace;
    for (std::size_t i = 0; i < n; ++i) {
        while (pids[i]->presence() == os::ProcessId::Presence::Alive && !deadline_passed(deadline))
            sleep_poll_slice(deadline);
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (pids[i]->presence() != os::ProcessId::Presence::Gone)
            return false;
    }
    return true;
}

}
