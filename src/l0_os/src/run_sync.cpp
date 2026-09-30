// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/run_sync.h"

#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace mister::os {
namespace {

constexpr long kPollStartNs = 200'000;
constexpr long kPollMaxNs = 10'000'000;
constexpr int kGraceMs = 500;

void sleep_ns(long ns) noexcept {
    struct timespec ts {
        0, ns
    };
    while (::clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, &ts) == EINTR) {
    }
}

constexpr long ramp(long ns) noexcept { return ns >= kPollMaxNs / 2 ? kPollMaxNs : ns * 2; }

int try_reap(::pid_t pid, int& status) noexcept {
    for (;;) {
        const ::pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r < 0 && errno == EINTR) continue;
        return static_cast<int>(r);
    }
}

std::vector<char*> to_argv(std::span<const char* const> argv) {
    std::vector<char*> args;
    args.reserve(argv.size() + 1);
    for (const char* a : argv)
        args.push_back(const_cast<char*>(a));
    args.push_back(nullptr);
    return args;
}

int decode(int status) noexcept { return WIFEXITED(status) ? WEXITSTATUS(status) : -1; }

constexpr const char* kShell = "/bin/sh";

bool never_started(int e) noexcept { return e == EAGAIN || e == ENOMEM; }

std::string script_path(const char* file) {
    if (std::strchr(file, '/') != nullptr) return file;
    const char* path = std::getenv("PATH");
    std::string_view rest{path != nullptr ? path : "/bin:/usr/bin"};
    for (;;) {
        const auto colon = rest.find(':');
        const std::string_view dir = rest.substr(0, colon);
        std::string cand{dir.empty() ? std::string_view{"."} : dir};
        cand += '/';
        cand += file;
        struct stat st {};
        if (::stat(cand.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
            ::access(cand.c_str(), X_OK) == 0) {
            return cand;
        }
        if (colon == std::string_view::npos) return {};
        rest.remove_prefix(colon + 1);
    }
}

std::expected<::pid_t, int> spawn_child(const std::vector<char*>& args) {
    ::pid_t pid = -1;
    int e = ::posix_spawnp(&pid, args[0], nullptr, nullptr, args.data(), environ);
    if (e == 0) return pid;
    if (e != ENOEXEC) return std::unexpected(e);
    std::string file = script_path(args[0]);
    if (file.empty()) return std::unexpected(e);
    std::vector<char*> sh;
    sh.reserve(args.size() + 1);
    sh.push_back(const_cast<char*>(kShell));
    sh.push_back(file.data());
    sh.insert(sh.end(), args.begin() + 1, args.end());
    e = ::posix_spawn(&pid, kShell, nullptr, nullptr, sh.data(), environ);
    if (e != 0) return std::unexpected(e);
    return pid;
}

}  // namespace

Ex<int> run_sync(std::span<const char* const> argv, std::int32_t timeout_ms) {
    if (argv.empty()) return std::unexpected(Error{Errc::io, ERR_SITE(), 0});

    std::vector<char*> args = to_argv(argv);

    const auto spawned = spawn_child(args);
    if (!spawned) {
        const int e = spawned.error();
        if (never_started(e)) {
            return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(e)});
        }
        return 127;
    }
    const ::pid_t pid = *spawned;

    int status = 0;

    if (timeout_ms <= 0) {
        for (;;) {
            const ::pid_t r = ::waitpid(pid, &status, 0);
            if (r < 0 && errno == EINTR) continue;
            if (r < 0) {
                return std::unexpected(
                    Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
            }
            return decode(status);
        }
    }

    const std::int64_t bound_ns = static_cast<std::int64_t>(timeout_ms) * 1'000'000;
    long ivl = kPollStartNs;
    for (std::int64_t waited_ns = 0; waited_ns < bound_ns; waited_ns += ivl, ivl = ramp(ivl)) {
        const int r = try_reap(pid, status);
        if (r > 0) return decode(status);
        if (r < 0) {
            return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
        }
        sleep_ns(ivl);
    }

    if (try_reap(pid, status) > 0) return decode(status);

    (void)::kill(pid, SIGTERM);
    for (int waited = 0; waited < kGraceMs; waited += 10) {
        if (try_reap(pid, status) > 0) {
            return std::unexpected(Error{Errc::timeout, ERR_SITE(), 0});
        }
        sleep_ns(kPollMaxNs);
    }
    (void)::kill(pid, SIGKILL);

    for (;;) {
        const ::pid_t r = ::waitpid(pid, &status, 0);
        if (r < 0 && errno == EINTR) continue;
        break;
    }
    return std::unexpected(Error{Errc::timeout, ERR_SITE(), 0});
}

Ex<void> run_detached(std::span<const char* const> argv) {
    if (argv.empty() || argv[0] == nullptr || argv[0][0] != '/') {
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    }

    std::vector<char*> args;
    args.reserve(argv.size() + 4);
    args.push_back(const_cast<char*>("sh"));
    args.push_back(const_cast<char*>("-c"));
    args.push_back(const_cast<char*>("\"$0\" \"$@\" &"));
    for (const char* a : argv)
        args.push_back(const_cast<char*>(a));
    args.push_back(nullptr);

    ::posix_spawnattr_t attr;
    if (const int e = ::posix_spawnattr_init(&attr); e != 0) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(e)});
    }
    ::pid_t mid = -1;
    int e = ::posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
    if (e == 0) e = ::posix_spawn(&mid, kShell, nullptr, &attr, args.data(), environ);
    (void)::posix_spawnattr_destroy(&attr);
    if (e != 0) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(e)});
    }

    int status = 0;
    for (;;) {
        const ::pid_t r = ::waitpid(mid, &status, 0);
        if (r < 0 && errno == EINTR) continue;
        if (r < 0) {
            return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
        }
        break;
    }

    if (decode(status) != 0) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    }
    return {};
}

}  // namespace mister::os
