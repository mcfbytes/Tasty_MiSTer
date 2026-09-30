// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "infra/error.h"
#include "infra/seat.h"
#include "infra/seat_main.h"
#include "infra/seat_park.h"
#include "infra/wake_flag.h"

namespace mister::app {
class SessionOwner;
}

namespace mister::fw {

class ThreadAssembly;

inline constexpr int kSupervisorWakeMs = 1000;

class ProcessMain final : public xthread::SeatMain<ProcessMain> {
    TASTY_SEAT_EXEMPT(main);

public:
    ProcessMain(app::SessionOwner& owner, const ThreadAssembly& assembly,
                xthread::WakeFlag& wake) noexcept;
    ProcessMain(const ProcessMain&) = delete;
    ProcessMain& operator=(const ProcessMain&) = delete;
    ProcessMain(ProcessMain&&) = delete;
    ProcessMain& operator=(ProcessMain&&) = delete;

    [[nodiscard]] Ex<void> open() noexcept;

    void start() noexcept;

    void stop() noexcept { stop_.request(); }

    [[nodiscard]] xthread::WakeFlag& stop_wake() noexcept { return stop_; }

    void serve() noexcept;
    [[nodiscard]] bool idle() const noexcept;
    [[nodiscard]] bool stopping() const noexcept;
    [[nodiscard]] int park_ms() const noexcept;
    [[nodiscard]] xthread::SeatPark& park() noexcept { return *park_; }

    [[nodiscard]] bool paused() noexcept { return false; }
    [[nodiscard]] bool pause_pending() const noexcept { return false; }
    void settle() noexcept {}

private:
    app::SessionOwner& owner_;
    const ThreadAssembly& assembly_;
    xthread::WakeFlag& wake_;
    xthread::WakeFlag stop_{};
    std::optional<xthread::SeatPark> park_{};
};

static_assert(xthread::SeatBody<ProcessMain>);

}  // namespace mister::fw
