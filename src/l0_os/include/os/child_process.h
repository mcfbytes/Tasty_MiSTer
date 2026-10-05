// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <optional>
#include <span>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::os {

inline constexpr int kChildKeptFd = 3;

struct ChildProcessSpec {
    std::span<const char* const> argv;
    std::span<const char* const> envp;
    const char* tty = nullptr;
    int kept_fd = -1;
};

struct ChildProcessExit {
    bool signalled = false;
    int code = 0;
};

class ChildProcess {
    TASTY_SEAT_EXEMPT(component);

public:
    [[nodiscard]] static Ex<ChildProcess> spawn(const ChildProcessSpec& spec);

    ChildProcess(ChildProcess&& o) noexcept;
    ChildProcess& operator=(ChildProcess&& o) noexcept;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    ~ChildProcess();

    [[nodiscard]] ::pid_t pid() const noexcept { return pid_; }

    [[nodiscard]] int pidfd() const noexcept { return pidfd_; }
    [[nodiscard]] bool reaped() const noexcept { return pid_ <= 0; }

    [[nodiscard]] Ex<void> signal(int sig) noexcept;

    [[nodiscard]] std::optional<ChildProcessExit> try_reap() noexcept;

private:
    ChildProcess(::pid_t pid, int pidfd) noexcept : pid_(pid), pidfd_(pidfd) {}
    void release_() noexcept;

    ::pid_t pid_ = -1;
    int pidfd_ = -1;
};

}  // namespace mister::os
