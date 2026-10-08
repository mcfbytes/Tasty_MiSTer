// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>

#include "app/launcher_demand.h"
#include "app/launcher_fb_ack.h"
#include "app/launcher_state.h"
#include "app/scanout_channel.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"
#include "os/child_process.h"
#include "os/clock.h"
#include "os/vt_console.h"

namespace mister::app {

class LauncherHost {
    TASTY_SEAT_RESIDENT(Launcher);

public:
    struct Wiring {
        const LauncherDemandCell* demand = nullptr;
        LauncherStateCell* state = nullptr;
        const LauncherFbAckCell* fb_ack = nullptr;
        ScanoutChannel* scanout = nullptr;
        os::IVtConsole* console = nullptr;
        const os::IClock* clock = nullptr;
    };

    struct Timing {
        std::chrono::milliseconds respawn{1000};
        std::chrono::milliseconds kill_after{400};
        std::chrono::milliseconds settle_limit{1000};
        std::chrono::milliseconds fb_ack_limit{500};
        std::chrono::milliseconds healthy_run{10000};
        unsigned strikes = 3;
    };

    static constexpr int kExitReload = 42;

    struct Counters {
        std::uint32_t spawns = 0;
        std::uint32_t spawn_failures = 0;
        std::uint32_t exits = 0;
        std::uint32_t kills = 0;
        std::uint32_t strikes = 0;
        std::uint32_t leases = 0;
        std::uint32_t refusals = 0;
        std::uint32_t bursts = 0;
        std::uint32_t invalid_packets = 0;
        std::uint32_t stale_swept = 0;
    };

    class Fds {
    public:
        [[nodiscard]] static Ex<Fds> create() noexcept;
        Fds(Fds&&) noexcept = default;
        Fds(const Fds&) = delete;
        Fds& operator=(const Fds&) = delete;
        Fds& operator=(Fds&&) = delete;

    private:
        friend class LauncherHost;
        explicit Fds(UniqueFd epoll) noexcept : epoll_(std::move(epoll)) {}
        UniqueFd epoll_;
    };

    LauncherHost(Fds fds, const Wiring& w, const Timing& t) noexcept
        : w_(w), t_(t), epoll_(std::move(fds.epoll_)) {}
    LauncherHost(Fds fds, const Wiring& w) noexcept : LauncherHost(std::move(fds), w, Timing{}) {}
    LauncherHost(const LauncherHost&) = delete;
    LauncherHost& operator=(const LauncherHost&) = delete;
    [[nodiscard]] int poll_fd() const noexcept { return epoll_.get(); }

    void serve() noexcept;

    void pause() noexcept;
    void resume() noexcept;
    [[nodiscard]] bool quiescent() const noexcept;

    [[nodiscard]] bool idle() const noexcept;

    [[nodiscard]] int park_ms() const noexcept;

    [[nodiscard]] const Counters& counters() const noexcept { return n_; }
    [[nodiscard]] bool child_running() const noexcept { return child_.has_value(); }
    [[nodiscard]] bool lease_open() const noexcept { return lease_.valid(); }

private:
    using Ns = std::chrono::nanoseconds;

    [[nodiscard]] Ns now_() const noexcept { return w_.clock->now(); }
    void step_() noexcept;
    void take_demand_() noexcept;
    [[nodiscard]] bool wanted_() const noexcept;
    [[nodiscard]] bool hosting_() const noexcept;
    void claim_screen_() noexcept;
    void spawn_() noexcept;
    void settle_() noexcept;
    void reap_() noexcept;
    void on_exit_(const os::ChildProcessExit& e) noexcept;
    void begin_stop_() noexcept;
    void release_screen_(bool restore_console) noexcept;
    void sweep_stale_() noexcept;
    void serve_lease_() noexcept;
    void grant_() noexcept;
    void finish_grant_() noexcept;
    void on_packet_(const std::byte* data, std::size_t n) noexcept;
    void deliver_answers_() noexcept;
    void close_lease_() noexcept;
    void watch_(int fd) noexcept;
    void unwatch_(int fd) noexcept;
    void publish_() noexcept;

    Wiring w_;
    Timing t_;
    UniqueFd epoll_;
    LauncherDemandCell::Reader demand_seen_{};
    LauncherDemand demand_{};
    std::optional<os::ChildProcess> child_{};
    UniqueFd lease_{};
    bool lease_granted_ = false;
    bool request_waiting_ = false;
    bool burst_in_flight_ = false;
    std::optional<UioBurst> held_ask_{};
    LauncherState state_{};
    bool paused_ = false;
    bool stopping_ = false;
    bool claiming_ = false;
    bool swept_ = false;
    std::uint32_t escaped_gen_ = 0;
    std::uint32_t given_up_gen_ = 0;
    unsigned strikes_ = 0;
    int prior_vt_ = 0;
    std::optional<Ns> respawn_at_{};
    std::optional<Ns> kill_at_{};
    std::optional<Ns> settle_by_{};
    std::optional<Ns> ack_by_{};
    std::optional<Ns> grant_by_{};
    Ns spawned_at_{};
    Counters n_{};
};

}  // namespace mister::app
