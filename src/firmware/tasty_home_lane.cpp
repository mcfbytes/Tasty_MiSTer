// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_home_lane.h"

#include <fcntl.h>
#include <linux/limits.h>
#include <signal.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <utility>

#include "infra/persist.h"
#include "tasty_cli_refusal.h"
#include "tasty_owner_lock.h"
#include "tasty_return_home.h"
#include "tasty_stock_control.h"

namespace mister::fw {

namespace {

enum class Claim : int { Free, Probed, Taken };

std::atomic<HomeLane::Lane> g_lane TASTY_PERSIST(proc, tasty_home_owed){HomeLane::Lane::Idle};
std::atomic<RunExit> g_deferred TASTY_PERSIST(proc, tasty_deferred_stop){RunExit::Done};
std::atomic<Claim> g_claim TASTY_PERSIST(proc, tasty_home_claimed){Claim::Free};

std::atomic<HomeLane::LaunchCount> g_spawned TASTY_PERSIST(proc, tasty_home_spawned){};
static_assert(std::atomic<HomeLane::Lane>::is_always_lock_free);
static_assert(std::atomic<RunExit>::is_always_lock_free);
static_assert(std::atomic<Claim>::is_always_lock_free);
static_assert(std::atomic<HomeLane::LaunchCount>::is_always_lock_free);

char g_home_exe[PATH_MAX] TASTY_PERSIST(proc, tasty_home_exe) = "/media/fat/MiSTer";
char g_home_menu[PATH_MAX] TASTY_PERSIST(proc, tasty_home_menu) = "menu.rbf";
char g_home_env[] TASTY_PERSIST(proc, tasty_home_env) = "PATH=/media/fat:/usr/bin:/bin";

}

void HomeLane::bind(const Bind& b) noexcept {
    std::snprintf(g_home_exe, sizeof g_home_exe, "%s", b.image.exe);
    std::snprintf(g_home_menu, sizeof g_home_menu, "%s", b.image.arg);
    g_spawned.store(LaunchCount{}, std::memory_order_seq_cst);
    g_claim.store(
        b.already_homed ? Claim::Taken : (b.probe ? Claim::Probed : Claim::Free),
        std::memory_order_seq_cst
    );
}

void HomeLane::unbind() noexcept {
    Claim probe = Claim::Probed;
    (void)g_claim.compare_exchange_strong(probe, Claim::Free, std::memory_order_seq_cst);
}

void HomeLane::owe() noexcept {
    g_lane.store(Lane::Open, std::memory_order_seq_cst);
}

bool HomeLane::may_launch() noexcept {
    const Lane l = g_lane.load(std::memory_order_seq_cst);
    return l != Lane::Stopping && l != Lane::Left;
}

HomeLane::Lane HomeLane::lane() noexcept {
    return g_lane.load(std::memory_order_seq_cst);
}

HomeLane::LaunchCount HomeLane::launches() noexcept {
    return g_spawned.load(std::memory_order_seq_cst);
}

void HomeLane::launch() noexcept {
    if (!may_launch())
        return;
    Claim claim = g_claim.load(std::memory_order_seq_cst);
    if (claim == Claim::Taken)
        return;
    if (!g_claim.compare_exchange_strong(claim, Claim::Taken, std::memory_order_seq_cst))
        return;
    if (claim == Claim::Probed) {
        count_launch_();
        return;
    }
    spawn_();
}

void HomeLane::count_launch_() noexcept {
    LaunchCount seen = g_spawned.load(std::memory_order_seq_cst);
    seen.note();
    g_spawned.store(seen, std::memory_order_seq_cst);
}

Ex<void> HomeLane::stop_and_owe(const StockControl& stock) noexcept {
    g_deferred.store(RunExit::Done, std::memory_order_seq_cst);
    g_lane.store(Lane::Stopping, std::memory_order_seq_cst);
    bool gone = false;
    if (const auto stopped = stock.stop())
        gone = *stopped;
    if (!gone) {
        g_lane.store(Lane::Left, std::memory_order_seq_cst);
        const TastyRefusal why{TastyRefusal::MenuLeftRunning};
        why.say();
        return std::unexpected(why.error(ERR_SITE()));
    }
    owe();
    if (const RunExit d = g_deferred.exchange(RunExit::Done, std::memory_order_seq_cst); failed(d))
        launch_and_exit(d);
    return {};
}

void HomeLane::defer_stop(RunExit status) noexcept {
    g_deferred.store(status, std::memory_order_seq_cst);
}

void HomeLane::launch_and_exit(RunExit status) noexcept {
    launch();
    ::_exit(process_status(status));
}

ReturnHome::ReturnHome(const HomeLane::Bind& bind) noexcept {
    HomeLane::bind(bind);
}

const TastyOwnerLock& ReturnHome::adopt(TastyOwnerLock lock) noexcept {
    lock_ = std::move(lock);
    return *lock_;
}

ReturnHome::~ReturnHome() noexcept {
    if (lock_)
        lock_->release();
    HomeLane::launch();
    HomeLane::unbind();
}

void HomeLane::spawn_() noexcept {
    const pid_t child = ::fork();
    if (child > 0)
        return;
    if (child < 0)
        return;
    (void)::setsid();
    const pid_t gc = ::fork();
    if (gc > 0)
        ::_exit(0);
    if (gc < 0)
        ::_exit(1);
    const int n = ::open("/dev/null", O_RDWR | O_CLOEXEC);
    if (n >= 0) {
        (void)::dup2(n, 0);
        (void)::dup2(n, 1);
        (void)::dup2(n, STDERR_FILENO);
        if (n > STDERR_FILENO)
            ::close(n);
    }

    struct sigaction dfl{};
    dfl.sa_handler = SIG_DFL;
    ::sigemptyset(&dfl.sa_mask);
    const int owned[] = {SIGHUP, SIGPIPE, SIGINT, SIGTERM, SIGQUIT, SIGSEGV, SIGABRT, SIGBUS};
    for (const int sig : owned)
        (void)::sigaction(sig, &dfl, nullptr);
    sigset_t empty;
    ::sigemptyset(&empty);
    (void)::sigprocmask(SIG_SETMASK, &empty, nullptr);
    char* av[] = {g_home_exe, g_home_menu, nullptr};
    char* env[] = {g_home_env, nullptr};
    ::execve(g_home_exe, av, env);
    ::_exit(1);
}

}
