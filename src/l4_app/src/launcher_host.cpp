// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/launcher_host.h"

#include <dirent.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/scanout_packet.h"

extern char** environ;

namespace mister::app {
namespace {

using std::chrono::milliseconds;
using std::chrono::nanoseconds;

constexpr std::string_view kEnvLocale = "LC_ALL=en_US.UTF-8";
constexpr std::string_view kEnvHome = "HOME=/root";

constexpr milliseconds kSettlePoll{20};

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
    if (lease) env.push_back(fd_prefix + std::to_string(os::kChildKeptFd));
    return env;
}

bool send_text(int fd, std::string_view s) noexcept {
    return ::send(fd, s.data(), s.size(), MSG_DONTWAIT | MSG_NOSIGNAL) ==
           static_cast<ssize_t>(s.size());
}

}  // namespace

Ex<LauncherHost::Fds> LauncherHost::Fds::create() noexcept {
    UniqueFd epoll(::epoll_create1(EPOLL_CLOEXEC));
    if (!epoll.valid())
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    return Fds{std::move(epoll)};
}

void LauncherHost::watch_(int fd) noexcept {
    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    (void)::epoll_ctl(epoll_.get(), EPOLL_CTL_ADD, fd, &ev);
}

void LauncherHost::unwatch_(int fd) noexcept {
    (void)::epoll_ctl(epoll_.get(), EPOLL_CTL_DEL, fd, nullptr);
}

void LauncherHost::publish_() noexcept {
    if (w_.state != nullptr) w_.state->publish(state_);
}

void LauncherHost::take_demand_() noexcept {
    if (w_.demand == nullptr) return;
    LauncherDemand d{};
    if (!demand_seen_.take_if_changed(*w_.demand, d)) return;
    if (d.core_gen != demand_.core_gen) strikes_ = 0;
    demand_ = d;
}

bool LauncherHost::hosting_() const noexcept {
    if (demand_.profile == nullptr || !demand_.front_end || demand_.program.empty()) return false;
    return escaped_gen_ != demand_.core_gen && given_up_gen_ != demand_.core_gen;
}

bool LauncherHost::wanted_() const noexcept { return !paused_ && hosting_(); }

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
    if (!swept_ && demand_.profile != nullptr && !demand_.program.empty()) sweep_stale_();
    reap_();
    deliver_answers_();
    serve_lease_();
    finish_grant_();
    const Ns now = now_();
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
        const bool acked =
            w_.fb_ack != nullptr && w_.fb_ack->sample().value.screen_gen == state_.screen_gen;
        if (acked || (ack_by_ && now >= *ack_by_)) spawn_();
        return;
    }
    if (!wanted_()) {
        respawn_at_.reset();
        if (state_.owns_screen) release_screen_(false);
        return;
    }
    if (respawn_at_ && now < *respawn_at_) return;
    respawn_at_.reset();
    claim_screen_();
}

void LauncherHost::claim_screen_() noexcept {
    if (!state_.owns_screen && w_.console != nullptr) {
        if (const auto vt = w_.console->active(); vt && *vt != demand_.profile->vt) prior_vt_ = *vt;
        const std::string tty(demand_.profile->tty);
        w_.console->blank(tty.c_str());
    }
    ++state_.screen_gen;
    if (state_.screen_gen == 0) state_.screen_gen = 1;
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
    const std::string device(p.scanout_device);

    if (!device.empty() && demand_.scanout_core && !demand_.direct_video &&
        ::access(device.c_str(), F_OK) == 0) {
        int sv[2] = {-1, -1};
        if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0, sv) == 0) {
            lease_.reset(sv[0]);
            child_end.reset(sv[1]);
        }
    }
    const std::vector<std::string> env = child_env(child_end.valid());
    std::vector<const char*> envp;
    envp.reserve(env.size());
    for (const std::string& e : env)
        envp.push_back(e.c_str());
    const std::array<const char*, 1> argv{demand_.program.c_str()};
    const std::string tty(p.tty);
    auto c = os::ChildProcess::spawn(
        {.argv = argv, .envp = envp, .tty = tty.c_str(), .kept_fd = child_end.get()});
    if (!c) {
        ++n_.spawn_failures;
        close_lease_();
        on_exit_(os::ChildProcessExit{.signalled = false, .code = -1});
        return;
    }
    ++n_.spawns;
    child_.emplace(std::move(*c));
    watch_(child_->pidfd());
    if (lease_.valid()) watch_(lease_.get());
    if (w_.console != nullptr) (void)w_.console->activate(p.vt);
    settle_by_ = spawned_at_ + nanoseconds{t_.settle_limit};
}

void LauncherHost::settle_() noexcept {
    if (state_.settled) return;
    const bool on_vt = w_.console != nullptr && w_.console->active() == demand_.profile->vt;
    if (!on_vt && settle_by_ && now_() < *settle_by_) return;
    state_.settled = true;
    settle_by_.reset();
    publish_();
    if (request_waiting_) grant_();
}

void LauncherHost::grant_() noexcept {
    request_waiting_ = false;
    if (!lease_.valid()) return;
    state_.scanout_owned = true;
    publish_();
    grant_by_ = now_() + nanoseconds{t_.fb_ack_limit};
}

void LauncherHost::finish_grant_() noexcept {
    if (!grant_by_ || !lease_.valid()) return;
    if (!(w_.fb_ack != nullptr && w_.fb_ack->sample().value.held)) {
        if (now_() < *grant_by_) return;
        ++n_.refusals;
        (void)send_text(lease_.get(), scanout::kRefuse);
        close_lease_();
        return;
    }
    grant_by_.reset();
    if (!send_text(lease_.get(), scanout::kGrant)) {
        close_lease_();
        return;
    }
    lease_granted_ = true;
    ++n_.leases;
}

void LauncherHost::reap_() noexcept {
    if (!child_) return;
    const auto e = child_->try_reap();
    if (!e) return;
    child_.reset();
    close_lease_();
    on_exit_(*e);
}

void LauncherHost::on_exit_(const os::ChildProcessExit& e) noexcept {
    ++n_.exits;
    kill_at_.reset();
    settle_by_.reset();
    if (stopping_) {
        stopping_ = false;
        release_screen_(false);
        return;
    }
    const Ns now = now_();
    if (!e.signalled && e.code == 0) {
        escaped_gen_ = demand_.core_gen;
        strikes_ = 0;
        release_screen_(true);
        return;
    }
    if (!e.signalled && e.code == kExitReload) {

        respawn_at_ = std::max(now, spawned_at_ + nanoseconds{t_.respawn});
        return;
    }
    if (now - spawned_at_ >= nanoseconds{t_.healthy_run}) strikes_ = 0;
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
    if (!child_ || stopping_) return;
    stopping_ = true;
    (void)child_->signal(SIGTERM);
    kill_at_ = now_() + nanoseconds{t_.kill_after};
}

void LauncherHost::release_screen_(bool restore_console) noexcept {
    const bool had = state_.owns_screen || state_.scanout_owned;
    state_.owns_screen = false;
    state_.settled = false;
    state_.scanout_owned = false;
    if (had) publish_();
    if (restore_console && w_.console != nullptr && demand_.profile != nullptr) {
        const std::string tty(demand_.profile->tty);
        w_.console->restore_text(tty.c_str());
        if (prior_vt_ > 0) (void)w_.console->activate(prior_vt_);
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
    if (w_.demand != nullptr) {
        const std::uint32_t g = w_.demand->generation();
        if (g != 0 && g != demand_seen_.seen()) return false;
    }
    if (grant_by_ && w_.fb_ack != nullptr && w_.fb_ack->sample().value.held) return false;
    if (claiming_ && w_.fb_ack != nullptr &&
        w_.fb_ack->sample().value.screen_gen == state_.screen_gen)
        return false;
    return w_.scanout == nullptr || w_.scanout->answers_empty();
}

bool LauncherHost::quiescent() const noexcept {
    return !child_ && !claiming_ && !lease_.valid() && !burst_in_flight_;
}

int LauncherHost::park_ms() const noexcept {
    std::optional<Ns> due{};
    const auto consider = [&due](const std::optional<Ns>& d) noexcept {
        if (d && (!due || *d < *due)) due = d;
    };
    consider(respawn_at_);
    consider(kill_at_);
    consider(grant_by_);
    if (held_ask_) consider(now_() + nanoseconds{kSettlePoll});
    if (claiming_) consider(ack_by_);
    std::optional<Ns> settle{};
    if (child_ && !state_.settled && settle_by_) {
        settle = std::min(*settle_by_, now_() + nanoseconds{kSettlePoll});
        consider(settle);
    }
    if (!due) return -1;
    const Ns left = *due - now_();
    if (left.count() <= 0) return 0;
    const auto ms = std::chrono::ceil<milliseconds>(left).count();
    return ms > INT_MAX ? INT_MAX : static_cast<int>(ms);
}

void LauncherHost::sweep_stale_() noexcept {
    swept_ = true;
    DIR* d = ::opendir("/proc");
    if (d == nullptr) return;
    const std::string_view want = demand_.program.view();
    const ::pid_t self = ::getpid();
    while (const dirent* ent = ::readdir(d)) {
        char* end = nullptr;
        const long pid = std::strtol(ent->d_name, &end, 10);
        if (end == ent->d_name || *end != '\0' || pid <= 0 || pid == self) continue;
        char link[64];
        (void)std::snprintf(link, sizeof link, "/proc/%ld/exe", pid);
        char target[PATH_MAX];
        const ssize_t n = ::readlink(link, target, sizeof target - 1);
        if (n <= 0) continue;
        std::string_view exe(target, static_cast<std::size_t>(n));
        constexpr std::string_view kDeleted = " (deleted)";
        if (exe.size() > kDeleted.size() && exe.substr(exe.size() - kDeleted.size()) == kDeleted)
            exe.remove_suffix(kDeleted.size());
        if (exe != want) continue;
        if (::kill(static_cast<::pid_t>(pid), SIGKILL) == 0) ++n_.stale_swept;
    }
    ::closedir(d);
}

void LauncherHost::serve_lease_() noexcept {
    if (held_ask_ && !burst_in_flight_ && w_.scanout != nullptr && w_.scanout->post(*held_ask_)) {
        held_ask_.reset();
        burst_in_flight_ = true;
    }
    while (lease_.valid()) {
        std::byte buf[64];
        const ssize_t n = ::recv(lease_.get(), buf, sizeof buf, MSG_DONTWAIT);
        if (n == 0) {
            close_lease_();
            return;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) close_lease_();
            return;
        }
        on_packet_(buf, static_cast<std::size_t>(n));
    }
}

void LauncherHost::on_packet_(const std::byte* data, std::size_t n) noexcept {
    if (!lease_granted_) {
        const std::string_view text(reinterpret_cast<const char*>(data), n);
        if (text != scanout::kRequest || request_waiting_) {
            ++n_.refusals;
            (void)send_text(lease_.get(), scanout::kRefuse);
            close_lease_();
            return;
        }
        request_waiting_ = true;
        if (state_.settled) grant_();
        return;
    }

    if (burst_in_flight_ || held_ask_ || w_.scanout == nullptr) {
        ++n_.invalid_packets;
        return;
    }
    const auto b = scanout::decode(std::span<const std::byte>(data, n));
    if (!b) {
        ++n_.invalid_packets;
        return;
    }
    if (w_.scanout->post(*b))
        burst_in_flight_ = true;
    else
        held_ask_ = *b;
}

void LauncherHost::deliver_answers_() noexcept {
    if (w_.scanout == nullptr) return;
    while (const auto a = w_.scanout->take_answer()) {
        burst_in_flight_ = false;
        ++n_.bursts;
        if (!lease_.valid() || !lease_granted_) continue;

        if (!a->ok) {
            close_lease_();
            continue;
        }
        std::array<std::byte, scanout::kMaxPacketBytes> out{};
        const std::size_t len = scanout::encode_reply(*a, out);
        if (len == 0 || ::send(lease_.get(), out.data(), len, MSG_DONTWAIT | MSG_NOSIGNAL) !=
                            static_cast<ssize_t>(len))
            close_lease_();
    }
}

void LauncherHost::close_lease_() noexcept {
    if (lease_.valid()) {
        unwatch_(lease_.get());
        lease_.reset();
    }
    lease_granted_ = false;
    request_waiting_ = false;
    grant_by_.reset();
    held_ask_.reset();
    if (state_.scanout_owned) {
        state_.scanout_owned = false;
        publish_();
    }
}

}  // namespace mister::app
