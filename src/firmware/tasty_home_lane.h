// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <cstdint>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "run_exit.h"
#include "tasty_paths.h"

namespace mister::fw {

class StockControl;

class HomeLane {
    TASTY_SEAT_RESIDENT(Main);

public:
    enum class Lane : int { Idle, Open, Stopping, Left };

    struct Image {
        const char* exe = kStockMisterPath;
        const char* arg = kMenuRbfName;
    };

    struct LaunchCount {
        std::uint32_t v = 0;

        template <std::integral I>
        [[nodiscard]] static constexpr LaunchCount of(I n) noexcept {
            return LaunchCount{static_cast<std::uint32_t>(n)};
        }

        void note() noexcept { ++v; }

        friend constexpr bool operator==(LaunchCount, LaunchCount) = default;
    };

    struct Bind {
        Image image{};
        bool probe = false;
        bool already_homed = false;
    };

    static void bind(const Bind& b) noexcept;

    static void unbind() noexcept;

    static void owe() noexcept;

    [[nodiscard]] static bool may_launch() noexcept;
    [[nodiscard]] static Lane lane() noexcept;

    [[nodiscard]] static LaunchCount launches() noexcept;

    static void launch() noexcept;

    [[nodiscard]] static Ex<void> stop_and_owe(const StockControl& stock) noexcept;

    static void defer_stop(RunExit status) noexcept;
    [[noreturn]] static void launch_and_exit(RunExit status) noexcept;

private:
    static void count_launch_() noexcept;
    static void spawn_() noexcept;
};

}
