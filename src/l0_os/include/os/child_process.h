// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <optional>
#include <span>

#include "infra/error.h"
#include "infra/path_text.h"
#include "infra/seat.h"
#include "os/process_id.h"

namespace mister::os {

inline constexpr int kChildKeptFd = 3;

struct ChildProcessSpec {
    std::span<const char* const> argv;
    std::span<const char* const> envp;
    infra::PathText tty{};
    int kept_fd = -1;
};

class ChildProcessExit {
public:
    struct ExitStatus {
        int v = 0;

        [[nodiscard]] constexpr bool is_success() const noexcept { return v == 0; }

        friend constexpr bool operator==(ExitStatus, ExitStatus) = default;
    };

    struct SignalNumber {
        int v = 0;

        struct StatusBase {
            int v = 0;
            friend constexpr bool operator==(StatusBase, StatusBase) = default;
        };

        [[nodiscard]] constexpr int shell_status(StatusBase base) const noexcept {
            return base.v + v;
        }

        friend constexpr bool operator==(SignalNumber, SignalNumber) = default;
    };

    struct WaitStatus {
        int v = 0;
        [[nodiscard]] bool signalled() const noexcept;
        [[nodiscard]] ExitStatus exit_status() const noexcept;
        [[nodiscard]] SignalNumber signal() const noexcept;
    };

    [[nodiscard]] static ChildProcessExit from_wait_status(WaitStatus status) noexcept;

    [[nodiscard]] static constexpr ChildProcessExit exited(ExitStatus status) noexcept {
        return ChildProcessExit{Kind::Exited, status, SignalNumber{}};
    }

    [[nodiscard]] static constexpr ChildProcessExit killed_by(SignalNumber signal) noexcept {
        return ChildProcessExit{Kind::Signalled, ExitStatus{}, signal};
    }

    [[nodiscard]] static constexpr ChildProcessExit failed() noexcept {
        return ChildProcessExit{Kind::ReapFailed, ExitStatus{}, SignalNumber{}};
    }

    [[nodiscard]] constexpr bool succeeded() const noexcept {
        return kind_ == Kind::Exited && status_.is_success();
    }

    [[nodiscard]] constexpr bool signalled() const noexcept { return kind_ == Kind::Signalled; }

    [[nodiscard]] constexpr bool reap_failed() const noexcept { return kind_ == Kind::ReapFailed; }

    [[nodiscard]] constexpr bool exited_with(ExitStatus status) const noexcept {
        return kind_ == Kind::Exited && status_ == status;
    }

    [[nodiscard]] constexpr std::optional<ExitStatus> exit_status() const noexcept {
        if (kind_ != Kind::Exited)
            return std::nullopt;
        return status_;
    }

    [[nodiscard]] constexpr std::optional<SignalNumber> signal() const noexcept {
        if (kind_ != Kind::Signalled)
            return std::nullopt;
        return signal_;
    }

private:
    enum class Kind : std::uint8_t { Exited, Signalled, ReapFailed };

    constexpr ChildProcessExit(Kind kind, ExitStatus status, SignalNumber signal) noexcept
        : kind_(kind),
          status_(status),
          signal_(signal) {}

    Kind kind_ = Kind::Exited;
    ExitStatus status_{};
    SignalNumber signal_{};
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

    [[nodiscard]] std::optional<ProcessId> pid() const noexcept { return ProcessId::of(pid_); }

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

}
