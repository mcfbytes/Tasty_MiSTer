// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/launcher_host.h"

#include <dirent.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <format>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/scanout_packet.h"
#include "infra/fixed_str.h"
#include "infra/parse_int.h"
#include "infra/path_text.h"
#include "os/path_buf.h"

extern char** environ;

namespace mister::app {
namespace {

using std::chrono::milliseconds;
using std::chrono::nanoseconds;

constexpr std::string_view kEnvLocale = "LC_ALL=en_US.UTF-8";
constexpr std::string_view kEnvHome = "HOME=/root";

constexpr milliseconds kSettlePoll{20};

constexpr std::size_t kProcLinkBytes = 64;
using ProcLink = FixedStr<kProcLinkBytes, StrFit::Reject>;

bool starts_with(std::string_view s, std::string_view p) noexcept {
    return s.size() >= p.size() && s.substr(0, p.size()) == p;
}

std::vector<std::string> child_env(bool lease) {
    std::vector<std::string> env;
    const std::string fd_prefix = std::string(scanout::kFdEnv) + "=";
    for (char** e = environ; e != nullptr && *e != nullptr; ++e) {
        const std::string_view v{*e};
        if (starts_with(v, "LC_ALL=") || starts_with(v, "HOME=") || starts_with(v, fd_prefix))
            continue;
        env.emplace_back(v);
    }
    env.emplace_back(kEnvLocale);
    env.emplace_back(kEnvHome);
    if (lease)
        env.push_back(fd_prefix + std::to_string(os::kChildKeptFd));
    return env;
}

}

void LauncherHost::watch_(int fd) noexcept {
    (void)epoll_.add(fd);
}

void LauncherHost::publish_() noexcept {
    w_.state.publish(state_);
}

void LauncherHost::sync_lease_() noexcept {
    if (lease_.routed() == state_.scanout_owned)
        return;
    state_.scanout_owned = lease_.routed();
    publish_();
}

void LauncherHost::take_demand_() noexcept {
    LauncherDemand d{};
    if (!demand_seen_.take_if_changed(w_.demand, d))
        return;
    if (d.core_gen != demand_.core_gen)
        strikes_ = 0;
    demand_ = d;
}

bool LauncherHost::hosting_() const noexcept {
    if (!demand_.profile || !demand_.front_end || demand_.program.empty())
        return false;
    return escaped_gen_ != demand_.core_gen && given_up_gen_ != demand_.core_gen;
}

bool LauncherHost::wanted_() const noexcept {
    return !paused_ && hosting_();
}

void LauncherHost::serve() noexcept {
    TASTY_SEAT_BODY(LauncherHost);
    step_();
    if (const bool h = hosting_(); h != state_.hosting) {
        state_.hosting = h;
        publish_();
    }
}

void LauncherHost::step_() noexcept {
    take_demand_();
    if (!swept_ && demand_.profile && !demand_.program.empty())
        sweep_stale_();
    reap_();
    const Ns now = now_();
    lease_.serve(now);
    sync_lease_();
    if (child_) {
        if (stopping_) {
            if (kill_at_ && now >= *kill_at_) {
                (void)child_->signal(SIGKILL);
                ++n_.kills;
                kill_at_.reset();
            }
        } else if (!wanted_()) {
            begin_stop_();
        } else {
            settle_();
        }
        return;
    }
    if (claiming_) {
        if (!wanted_()) {
            claiming_ = false;
            release_screen_(false);
            return;
        }
        const bool acked = w_.fb_ack.sample().value.screen_gen == state_.screen_gen;
        if (acked || (ack_by_ && now >= *ack_by_))
            spawn_();
        return;
    }
    if (!wanted_()) {
        respawn_at_.reset();
        if (state_.owns_screen)
            release_screen_(false);
        return;
    }
    if (respawn_at_ && now < *respawn_at_)
        return;
    respawn_at_.reset();
    claim_screen_();
}

void LauncherHost::claim_screen_() noexcept {
    if (!state_.owns_screen) {
        if (const auto vt = w_.console.active(); vt && *vt != demand_.profile->vt)
            prior_vt_ = *vt;
        w_.console.blank(demand_.profile->vt);
    }
    ++state_.screen_gen;
    if (state_.screen_gen == 0)
        state_.screen_gen = 1;
    state_.owns_screen = true;
    state_.settled = false;
    publish_();
    claiming_ = true;
    ack_by_ = now_() + nanoseconds{t_.fb_ack_limit};
}

void LauncherHost::spawn_() noexcept {
    claiming_ = false;
    ack_by_.reset();
    spawned_at_ = now_();
    const LauncherProfile& p = *demand_.profile;
    UniqueFd child_end{};
    UniqueFd lease_end{};
    const std::string device(p.scanout_device);

    if (!device.empty()
        && demand_.scanout_core
        && !demand_.direct_video
        && ::access(device.c_str(), F_OK) == 0) {
        int sv[2] = {-1, -1};
        if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, sv) == 0) {
            lease_end.reset(sv[0]);
            child_end.reset(sv[1]);
        }
    }
    const std::vector<std::string> env = child_env(child_end.valid());
    std::vector<const char*> envp;
    envp.reserve(env.size());
    for (const std::string& e : env)
        envp.push_back(e.c_str());
    const std::array<const char*, 1> argv{demand_.program.c_str()};

    std::array<char, os::kVtTtyPathBuf> tty_node{};
    std::string_view tty_text = w_.tty;
    if (tty_text.empty()) {
        if (const auto node = p.vt.tty_path(std::span<char>(tty_node)))
            tty_text = *node;
    }
    infra::PathText tty;
    if (!tty.assign(tty_text)) {
        ++n_.spawn_failures;
        on_spawn_failure_();
        return;
    }
    auto c = os::ChildProcess::spawn(
        {.argv = argv, .envp = envp, .tty = tty, .kept_fd = child_end.get()}
    );
    if (!c) {
        ++n_.spawn_failures;
        on_spawn_failure_();
        return;
    }
    ++n_.spawns;
    child_.emplace(std::move(*c));
    watch_(child_->pidfd());
    if (lease_end.valid()) {
        watch_(lease_end.get());
        lease_.offer(std::move(lease_end));
    }
    (void)w_.console.activate(p.vt);
    settle_by_ = spawned_at_ + nanoseconds{t_.settle_limit};
}

void LauncherHost::settle_() noexcept {
    if (state_.settled)
        return;
    const bool on_vt = w_.console.active() == demand_.profile->vt;
    if (!on_vt && settle_by_ && now_() < *settle_by_)
        return;
    state_.settled = true;
    settle_by_.reset();
    publish_();
    lease_.settled(now_());
    sync_lease_();
}

void LauncherHost::reap_() noexcept {
    if (!child_)
        return;
    const auto e = child_->try_reap();
    if (!e)
        return;
    child_.reset();
    lease_.close();
    sync_lease_();
    on_exit_(*e);
}

void LauncherHost::on_exit_(const os::ChildProcessExit& e) noexcept {
    if (end_attempt_())
        return;
    const Ns now = now_();
    if (e.succeeded()) {
        escaped_gen_ = demand_.core_gen;
        strikes_ = 0;
        release_screen_(true);
        return;
    }
    if (e.exited_with(kExitReload)) {

        respawn_at_ = std::max(now, spawned_at_ + nanoseconds{t_.respawn});
        return;
    }
    strike_(now);
}

void LauncherHost::on_spawn_failure_() noexcept {
    if (end_attempt_())
        return;
    strike_(now_());
}

bool LauncherHost::end_attempt_() noexcept {
    ++n_.exits;
    kill_at_.reset();
    settle_by_.reset();
    if (!stopping_)
        return false;
    stopping_ = false;
    release_screen_(false);
    return true;
}

void LauncherHost::strike_(Ns now) noexcept {
    if (now - spawned_at_ >= nanoseconds{t_.healthy_run})
        strikes_ = 0;
    ++strikes_;
    ++n_.strikes;
    if (strikes_ >= t_.strikes) {
        given_up_gen_ = demand_.core_gen;
        strikes_ = 0;
        release_screen_(true);
        return;
    }
    respawn_at_ = now + nanoseconds{t_.respawn};
}

void LauncherHost::begin_stop_() noexcept {
    if (!child_ || stopping_)
        return;
    stopping_ = true;
    (void)child_->signal(SIGTERM);
    kill_at_ = now_() + nanoseconds{t_.kill_after};
}

void LauncherHost::release_screen_(bool restore_console) noexcept {
    const bool had = state_.owns_screen || state_.scanout_owned;
    state_.owns_screen = false;
    state_.settled = false;
    state_.scanout_owned = false;
    if (had)
        publish_();
    if (restore_console && demand_.profile) {
        w_.console.restore_text(demand_.profile->vt);
        if (prior_vt_ != os::VtNumber{})
            (void)w_.console.activate(prior_vt_);
    }
}

void LauncherHost::pause() noexcept {
    TASTY_SEAT_BODY(LauncherHost);
    paused_ = true;
    respawn_at_.reset();
    if (child_) {
        begin_stop_();
    } else if (claiming_) {
        claiming_ = false;
        ack_by_.reset();
        release_screen_(false);
    } else if (state_.owns_screen) {
        release_screen_(false);
    }
}

void LauncherHost::resume() noexcept {
    TASTY_SEAT_BODY(LauncherHost);
    paused_ = false;
}

bool LauncherHost::idle() const noexcept {
    const std::uint32_t g = w_.demand.generation();
    if (g != 0 && g != demand_seen_.seen())
        return false;
    if (lease_.awaiting_hold() && w_.fb_ack.sample().value.held)
        return false;
    if (claiming_ && w_.fb_ack.sample().value.screen_gen == state_.screen_gen)
        return false;
    return w_.scanout.answers_empty();
}

bool LauncherHost::quiescent() const noexcept {
    return !child_ && !claiming_ && lease_.quiescent();
}

infra::ParkTimeout LauncherHost::park_ms() const noexcept {
    std::optional<Ns> due{};
    const auto consider = [&due](const std::optional<Ns>& d) noexcept {
        if (d && (!due || *d < *due))
            due = d;
    };
    consider(respawn_at_);
    consider(kill_at_);
    consider(lease_.due());
    if (lease_.holding())
        consider(now_() + nanoseconds{kSettlePoll});
    if (claiming_)
        consider(ack_by_);
    std::optional<Ns> settle{};
    if (child_ && !state_.settled && settle_by_) {
        settle = std::min(*settle_by_, now_() + nanoseconds{kSettlePoll});
        consider(settle);
    }
    if (!due)
        return infra::kForever;
    const Ns left = *due - now_();
    if (left.count() <= 0)
        return infra::kNoWait;
    const auto ms = std::chrono::ceil<milliseconds>(left);
    constexpr milliseconds kCeil{std::numeric_limits<std::int32_t>::max()};
    return std::chrono::duration_cast<infra::ParkTimeout>(std::min(ms, kCeil));
}

void LauncherHost::sweep_stale_() noexcept {
    swept_ = true;
    DIR* d = ::opendir("/proc");
    if (d == nullptr)
        return;
    const std::string_view want = demand_.program.view();
    const ::pid_t self = ::getpid();
    while (const dirent* ent = ::readdir(d)) {
        const long pid = infra::parse_int<long>(ent->d_name).value_or(0);
        if (pid <= 0 || pid == self)
            continue;

        std::array<char, kProcLinkBytes> text{};
        const auto written = std::format_to_n(text.data(), std::ssize(text), "/proc/{}/exe", pid);
        ProcLink link;
        if (written.size >= std::ssize(text)
            || !link.assign(std::string_view{text.data(), static_cast<std::size_t>(written.size)}))
            continue;
        char target[PATH_MAX];
        const ssize_t n = ::readlink(link.c_str(), target, sizeof target - 1);
        if (n <= 0)
            continue;
        std::string_view exe(target, static_cast<std::size_t>(n));
        constexpr std::string_view kDeleted = " (deleted)";
        if (exe.size() > kDeleted.size() && exe.substr(exe.size() - kDeleted.size()) == kDeleted)
            exe.remove_suffix(kDeleted.size());
        if (exe != want)
            continue;
        if (::kill(static_cast<::pid_t>(pid), SIGKILL) == 0)
            ++n_.stale_swept;
    }
    ::closedir(d);
}

}
