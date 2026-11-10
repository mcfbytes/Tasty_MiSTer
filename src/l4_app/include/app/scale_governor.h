// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>

#include "app/percent.h"
#include "app/rec_options.h"
#include "app/step_count.h"
#include "infra/seat.h"

namespace mister::app {

class ScaleGovernor {
    TASTY_SEAT_RESIDENT(Encode);

public:
    static constexpr std::uint8_t kSustainWindows = 3;

    static constexpr std::uint8_t kRecoverDoublings = 3;

    static constexpr std::size_t kBacklogResumeDivisor = 4;

    using Count = StepCount;

    struct QueueCount {
        std::size_t v = 0;

        template <std::integral I>
        [[nodiscard]] static constexpr QueueCount of(I n) noexcept {
            return QueueCount{static_cast<std::size_t>(n)};
        }

        [[nodiscard]] constexpr bool fills(QueueCount depth) const noexcept { return v >= depth.v; }

        [[nodiscard]] constexpr bool caught_up_to(QueueCount depth) const noexcept {
            return v * kBacklogResumeDivisor <= depth.v;
        }

        friend constexpr bool operator==(QueueCount, QueueCount) = default;
    };

    struct Limits {

        static constexpr std::chrono::seconds kBudgetWindow{2};
        static constexpr std::chrono::seconds kRecoverWindow{4};
        static constexpr Percent kBudgetPct = Percent::of(50);

        std::chrono::nanoseconds budget_window = kBudgetWindow;
        Percent budget_pct = kBudgetPct;

        std::chrono::nanoseconds recover = kRecoverWindow;
    };

    enum class Cause : std::uint8_t { None, Queue, Held };

    struct Change {
        bool changed = false;
        bool stepped = false;
        bool recovered = false;
        bool measured = false;
        Percent budget_pct{};
    };

    ScaleGovernor() noexcept = default;

    explicit ScaleGovernor(const Limits& l) noexcept : lim_(l) {}

    ScaleGovernor(const ScaleGovernor&) = delete;
    ScaleGovernor& operator=(const ScaleGovernor&) = delete;

    void restart(RecScale requested) noexcept;

    [[nodiscard]] PictureScale scale() const noexcept { return scale_; }

    [[nodiscard]] Cause cause() const noexcept { return cause_; }

    [[nodiscard]] bool fell_behind() const noexcept { return fell_behind_; }

    [[nodiscard]] Count steps() const noexcept { return steps_; }

    [[nodiscard]] Count recovered() const noexcept { return recovered_; }

    [[nodiscard]] Percent budget_pct() const noexcept { return lim_.budget_pct; }

    [[nodiscard]] Change on_encoded(
        std::chrono::nanoseconds cpu,
        std::chrono::nanoseconds now
    ) noexcept;

    [[nodiscard]] Change on_queue(
        QueueCount queued,
        QueueCount depth,
        bool held,
        std::chrono::nanoseconds now
    ) noexcept;

private:
    void step_(bool behind, std::chrono::nanoseconds now) noexcept;

    [[nodiscard]] bool too_soon_(std::chrono::nanoseconds now) const noexcept;

    Limits lim_{};
    bool auto_ = false;
    PictureScale scale_ = PictureScale::Native;
    Cause cause_ = Cause::None;
    bool fell_behind_ = false;
    std::chrono::nanoseconds calm_{};
    std::optional<std::chrono::nanoseconds> returned_{};

    std::optional<std::chrono::nanoseconds> win_start_{};
    std::chrono::nanoseconds win_cpu_{};
    Count win_over_{};
    Count steps_{};
    Count recovered_{};
};

}
