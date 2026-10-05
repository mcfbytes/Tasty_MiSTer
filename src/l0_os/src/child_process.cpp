// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/child_process.h"

#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <vector>

namespace mister::os {
namespace {

int open_pidfd(::pid_t pid) noexcept { return static_cast<int>(::syscall(SYS_pidfd_open, pid, 0)); }

std::unexpected<Error> os_error(std::uint16_t site, int e) {
    return std::unexpected(Error{Errc::os, site, static_cast<std::uint32_t>(e)});
}

std::vector<char*> terminated(std::span<const char* const> v) {
    std::vector<char*> out;
    out.reserve(v.size() + 1);
    for (const char* s : v)
        out.push_back(const_cast<char*>(s));
    out.push_back(nullptr);
    return out;
}

struct SpawnObjects {
    posix_spawn_file_actions_t fa{};
    posix_spawnattr_t attr{};
    bool fa_ok = false;
    bool attr_ok = false;
    SpawnObjects() noexcept {
        fa_ok = ::posix_spawn_file_actions_init(&fa) == 0;
        attr_ok = ::posix_spawnattr_init(&attr) == 0;
    }
    ~SpawnObjects() {
        if (fa_ok) ::posix_spawn_file_actions_destroy(&fa);
        if (attr_ok) ::posix_spawnattr_destroy(&attr);
    }
    SpawnObjects(const SpawnObjects&) = delete;
    SpawnObjects& operator=(const SpawnObjects&) = delete;
};

int add_file_actions(posix_spawn_file_actions_t& fa, const ChildProcessSpec& spec) noexcept {
    int first_closed = kChildKeptFd;
    if (spec.kept_fd >= 0) {
        if (const int rc = ::posix_spawn_file_actions_adddup2(&fa, spec.kept_fd, kChildKeptFd))
            return rc;
        first_closed = kChildKeptFd + 1;
    }
    if (spec.tty != nullptr) {
        if (const int rc = ::posix_spawn_file_actions_addopen(&fa, 0, spec.tty, O_RDWR, 0))
            return rc;
        if (const int rc = ::posix_spawn_file_actions_adddup2(&fa, 0, 1)) return rc;
        if (const int rc = ::posix_spawn_file_actions_adddup2(&fa, 0, 2)) return rc;
    }
    return ::posix_spawn_file_actions_addclosefrom_np(&fa, first_closed);
}

int add_attributes(posix_spawnattr_t& attr) noexcept {
    sigset_t none;
    sigset_t all;
    ::sigemptyset(&none);
    ::sigfillset(&all);
    if (const int rc = ::posix_spawnattr_setsigmask(&attr, &none)) return rc;
    if (const int rc = ::posix_spawnattr_setsigdefault(&attr, &all)) return rc;
    const short flags = POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
    return ::posix_spawnattr_setflags(&attr, flags);
}

}  // namespace

Ex<ChildProcess> ChildProcess::spawn(const ChildProcessSpec& spec) {
    if (spec.argv.empty() || spec.argv[0] == nullptr || spec.argv[0][0] != '/') {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    SpawnObjects o;
    if (!o.fa_ok || !o.attr_ok) return os_error(ERR_SITE(), ENOMEM);
    if (const int rc = add_file_actions(o.fa, spec)) return os_error(ERR_SITE(), rc);
    if (const int rc = add_attributes(o.attr)) return os_error(ERR_SITE(), rc);

    std::vector<char*> argv = terminated(spec.argv);
    std::vector<char*> envp = terminated(spec.envp);
    ::pid_t pid = -1;
    if (const int rc = ::posix_spawn(&pid, argv[0], &o.fa, &o.attr, argv.data(), envp.data()))
        return os_error(ERR_SITE(), rc);

    const int pidfd = open_pidfd(pid);
    if (pidfd < 0) {
        const int e = errno;
        (void)::kill(pid, SIGKILL);
        int st = 0;
        while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {
        }
        return os_error(ERR_SITE(), e);
    }
    return ChildProcess{pid, pidfd};
}

ChildProcess::ChildProcess(ChildProcess&& o) noexcept : pid_(o.pid_), pidfd_(o.pidfd_) {
    o.pid_ = -1;
    o.pidfd_ = -1;
}

ChildProcess& ChildProcess::operator=(ChildProcess&& o) noexcept {
    if (this != &o) {
        release_();
        pid_ = o.pid_;
        pidfd_ = o.pidfd_;
        o.pid_ = -1;
        o.pidfd_ = -1;
    }
    return *this;
}

ChildProcess::~ChildProcess() { release_(); }

void ChildProcess::release_() noexcept {
    if (pid_ > 0) {
        (void)::kill(-pid_, SIGKILL);
        (void)::kill(pid_, SIGKILL);
        int st = 0;
        while (::waitpid(pid_, &st, 0) < 0 && errno == EINTR) {
        }
        pid_ = -1;
    }
    if (pidfd_ >= 0) {
        (void)::close(pidfd_);
        pidfd_ = -1;
    }
}

Ex<void> ChildProcess::signal(int sig) noexcept {
    if (pid_ <= 0) return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});

    if (::kill(-pid_, sig) == 0) return {};
    if (::kill(pid_, sig) == 0) return {};
    return os_error(ERR_SITE(), errno);
}

std::optional<ChildProcessExit> ChildProcess::try_reap() noexcept {
    if (pid_ <= 0) return std::nullopt;
    int st = 0;
    ::pid_t r = -1;
    do {
        r = ::waitpid(pid_, &st, WNOHANG);
    } while (r < 0 && errno == EINTR);
    if (r == 0) return std::nullopt;
    pid_ = -1;
    if (pidfd_ >= 0) {
        (void)::close(pidfd_);
        pidfd_ = -1;
    }
    if (r < 0) return ChildProcessExit{.signalled = true, .code = 0};
    if (WIFSIGNALED(st)) return ChildProcessExit{.signalled = true, .code = WTERMSIG(st)};
    return ChildProcessExit{.signalled = false, .code = WIFEXITED(st) ? WEXITSTATUS(st) : 0};
}

}  // namespace mister::os
