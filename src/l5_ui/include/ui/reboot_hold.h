// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "infra/seat.h"

namespace mister::ui {

class RebootHold final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct RepeatDepth {
        std::size_t v = 0;
        friend constexpr bool operator==(RepeatDepth, RepeatDepth) = default;

        [[nodiscard]] constexpr bool released() const noexcept { return v == 0; }
    };

    struct ColdSlide {
        constexpr ColdSlide() noexcept = default;

        static constexpr std::string_view kHead = " Cold";
        static constexpr std::string_view kArmed = " Cold Reboot";

        [[nodiscard]] static constexpr ColdSlide at(RepeatDepth depth) noexcept {
            constexpr std::size_t kRepeatsPerSlide = 3;
            return ColdSlide{depth.v / kRepeatsPerSlide};
        }

        [[nodiscard]] static constexpr bool past_head(RepeatDepth depth) noexcept {
            constexpr std::size_t kRepeatsPerSlide = 3;
            return depth.v / kRepeatsPerSlide > kHead.size();
        }

        [[nodiscard]] constexpr bool complete() const noexcept { return v >= kHead.size(); }

        [[nodiscard]] constexpr std::string_view text() const noexcept {
            return kArmed.substr(kHead.size() - v);
        }

        friend constexpr bool operator==(ColdSlide, ColdSlide) = default;

    private:
        explicit constexpr ColdSlide(std::size_t reached) noexcept
            : v(reached < kHead.size() ? reached : kHead.size()) {}

        std::size_t v = 0;
    };

    struct Step {
        ColdSlide slide;
        bool ask_cold = false;
    };

    [[nodiscard]] Step hold(RepeatDepth depth) noexcept;

    void ask_landed() noexcept {
        if (phase_ == Phase::Sliding)
            phase_ = Phase::ColdSent;
    }

    [[nodiscard]] bool release(RepeatDepth depth) noexcept;

    [[nodiscard]] std::optional<std::string_view> tail() const noexcept {
        if (phase_ == Phase::Idle)
            return std::nullopt;
        return slide_.text();
    }

private:
    enum class Phase : std::uint8_t { Idle, Sliding, ColdSent };

    Phase phase_ = Phase::Idle;
    ColdSlide slide_{};
};

}
