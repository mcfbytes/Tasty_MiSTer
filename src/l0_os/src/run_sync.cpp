// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/run_sync.h"

#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mister::os {
namespace {

constexpr std::chrono::nanoseconds kPollStart{200'000};
constexpr std::chrono::nanoseconds kPollMax{10'000'000};
constexpr std::chrono::milliseconds kGrace{500};
constexpr std::chrono::milliseconds kGraceSlice{10};
constexpr std::chrono::nanoseconds::rep kRampFactor = 2;

constexpr int kExecFailedCode = 127;

void sleep_ns(std::chrono::nanoseconds d) noexcept {
    const auto secs = std::chrono::duration_cast<std::chrono::seconds>(d);
    timespec ts{};
    ts.tv_sec = static_cast<std::time_t>(secs.count());
    ts.tv_nsec = static_cast<long>((d - secs).count());
    while (::clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, &ts) == EINTR) {
    }
}

constexpr std::chrono::nanoseconds ramp(std::chrono::nanoseconds d) noexcept {
    return d >= kPollMax / kRampFactor ? kPollMax : d * kRampFactor;
}

struct ReapOutcome {
    enum class State : std::uint8_t { reaped, running, failed };
    State state = State::running;
    int status = 0;
    int error = 0;

    [[nodiscard]] static constexpr ReapOutcome reaped(int st) noexcept {
        return {State::reaped, st, 0};
    }

    [[nodiscard]] static constexpr ReapOutcome running() noexcept { return {State::running, 0, 0}; }

    [[nodiscard]] static constexpr ReapOutcome failed(int e) noexcept {
        return {State::failed, 0, e};
    }

    [[nodiscard]] ChildProcessExit exit() const noexcept {
        if (WIFSIGNALED(status))
            return ChildProcessExit{.signalled = true, .code = WTERMSIG(status)};
        return ChildProcessExit{
            .signalled = false,
            .code = WIFEXITED(status) ? WEXITSTATUS(status) : 0
        };
    }
};

[[nodiscard]] ReapOutcome try_reap(::pid_t pid) noexcept {
    int status = 0;
    for (;;) {
        const ::pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r < 0 && errno == EINTR)
            continue;
        if (r > 0)
            return ReapOutcome::reaped(status);
        if (r == 0)
            return ReapOutcome::running();
        return ReapOutcome::failed(errno);
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

constexpr const char* kShell = "/bin/sh";

bool never_started(int e) noexcept {
    return e == EAGAIN || e == ENOMEM;
}

std::string script_path(const char* file) {
    if (std::strchr(file, '/') != nullptr)
        return file;
    const char* path = std::getenv("PATH");
    std::string_view rest{path != nullptr ? path : "/bin:/usr/bin"};
    for (;;) {
        const auto colon = rest.find(':');
        const std::string_view dir = rest.substr(0, colon);
        std::string cand{dir.empty() ? std::string_view{"."} : dir};
        cand += '/';
        cand += file;
        struct stat st{};
        if (::stat(cand.c_str(), &st) == 0
            && S_ISREG(st.st_mode)
            && ::access(cand.c_str(), X_OK) == 0) {
            return cand;
        }
        if (colon == std::string_view::npos)
            return {};
        rest.remove_prefix(colon + 1);
    }
}

struct SpawnOutcome {
    enum class State : std::uint8_t { started, refused, exec_failed };
    State state = State::refused;
    ::pid_t pid = -1;
    int error = 0;

    [[nodiscard]] static constexpr SpawnOutcome started(::pid_t p) noexcept {
        return {State::started, p, 0};
    }

    [[nodiscard]] static constexpr SpawnOutcome refused(int e) noexcept {
        return {State::refused, -1, e};
    }

    [[nodiscard]] static constexpr SpawnOutcome exec_failed() noexcept {
        return {State::exec_failed, -1, 0};
    }
};

SpawnOutcome spawn_child(const std::vector<char*>& args) {
    ::pid_t pid = -1;
    int e = ::posix_spawnp(&pid, args[0], nullptr, nullptr, args.data(), environ);
    if (e == 0)
        return SpawnOutcome::started(pid);
    if (e != ENOEXEC)
        return SpawnOutcome::refused(e);
    std::string file = script_path(args[0]);
    if (file.empty())
        return SpawnOutcome::exec_failed();
    std::vector<char*> sh;
    sh.reserve(args.size() + 1);
    sh.push_back(const_cast<char*>(kShell));
    sh.push_back(file.data());
    sh.insert(sh.end(), args.begin() + 1, args.end());
    e = ::posix_spawn(&pid, kShell, nullptr, nullptr, sh.data(), environ);
    if (e != 0)
        return SpawnOutcome::refused(e);
    return SpawnOutcome::started(pid);
}

}

[[nodiscard]] Ex<ChildProcessExit> run_sync(
    std::span<const char* const> argv,
    std::chrono::milliseconds timeout
) {
    if (argv.empty())
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});

    std::vector<char*> args = to_argv(argv);

    const SpawnOutcome sp = spawn_child(args);
    if (sp.state == SpawnOutcome::State::refused) {
        if (never_started(sp.error)) {
            return std::unexpected(infra::errno_error(Errc::io, ERR_SITE(), sp.error));
        }
        return ChildProcessExit{.signalled = false, .code = kExecFailedCode};
    }
    if (sp.state == SpawnOutcome::State::exec_failed) {
        return ChildProcessExit{.signalled = false, .code = kExecFailedCode};
    }
    const ::pid_t pid = sp.pid;

    int status = 0;

    if (timeout.count() <= 0) {
        for (;;) {
            const ::pid_t r = ::waitpid(pid, &status, 0);
            if (r < 0 && errno == EINTR)
                continue;
            if (r < 0) {
                return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
            }
            return ReapOutcome::reaped(status).exit();
        }
    }

    const auto bound = std::chrono::duration_cast<std::chrono::nanoseconds>(timeout);
    std::chrono::nanoseconds ivl = kPollStart;
    for (std::chrono::nanoseconds waited{}; waited < bound; waited += ivl, ivl = ramp(ivl)) {
        const ReapOutcome re = try_reap(pid);
        if (re.state == ReapOutcome::State::reaped)
            return re.exit();
        if (re.state == ReapOutcome::State::failed) {
            return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), re.error));
        }
        sleep_ns(ivl);
    }

    if (const ReapOutcome re = try_reap(pid); re.state == ReapOutcome::State::reaped)
        return re.exit();

    (void)::kill(pid, SIGTERM);
    for (std::chrono::milliseconds waited{}; waited < kGrace; waited += kGraceSlice) {
        if (try_reap(pid).state == ReapOutcome::State::reaped) {
            return std::unexpected(Error{Errc::timeout, ERR_SITE(), 0});
        }
        sleep_ns(kPollMax);
    }
    (void)::kill(pid, SIGKILL);

    for (;;) {
        const ::pid_t r = ::waitpid(pid, &status, 0);
        if (r < 0 && errno == EINTR)
            continue;
        break;
    }
    return std::unexpected(Error{Errc::timeout, ERR_SITE(), 0});
}

[[nodiscard]] Ex<void> run_detached(std::span<const char* const> argv) {
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
        return std::unexpected(infra::errno_error(Errc::io, ERR_SITE(), e));
    }
    ::pid_t mid = -1;
    int e = ::posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
    if (e == 0)
        e = ::posix_spawn(&mid, kShell, nullptr, &attr, args.data(), environ);
    (void)::posix_spawnattr_destroy(&attr);
    if (e != 0) {
        return std::unexpected(infra::errno_error(Errc::io, ERR_SITE(), e));
    }

    int status = 0;
    for (;;) {
        const ::pid_t r = ::waitpid(mid, &status, 0);
        if (r < 0 && errno == EINTR)
            continue;
        if (r < 0) {
            return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
        }
        break;
    }

    if (ReapOutcome::reaped(status).exit().code != 0) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    }
    return {};
}

}
