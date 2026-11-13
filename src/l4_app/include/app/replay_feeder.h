// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "app/play_refusal.h"
#include "app/replay_play.h"
#include "app/replay_stage.h"
#include "app/replay_wiring.h"
#include "cores/movie_codec.h"
#include "infra/seat.h"

namespace mister::app {

class ReplayFeeder {
    TASTY_SEAT_RESIDENT(Ui);

public:
    using Wiring = ReplayWiring;
    using Stage = IReplayStage::Stage;
    using Refusal = PlayRefusal::Why;
    using SetSetting = ReplayPlay::SetSetting;

    struct Play {
        std::string_view movie{};
        std::string_view rom{};
        std::optional<std::chrono::microseconds> phase{};
        std::optional<FrameCount> lead{};

        std::optional<MovieFrame> stop_at{};

        bool set_settings = false;

        std::optional<cores::IReplayCore::RamFill> ram_fill{};

        bool seeded_save = false;
    };

    static constexpr std::uint32_t kMaxFrames = ReplayPlay::kMaxFrames;
    static constexpr std::chrono::nanoseconds kAnswerNs = ReplayPlay::kAnswerNs;
    static constexpr std::chrono::nanoseconds kDiscSettleNs = ReplayPlay::kDiscSettleNs;

    [[nodiscard]] static constexpr bool phase_fits(
        std::chrono::microseconds phase,
        std::chrono::nanoseconds period
    ) noexcept {
        return ReplayPlay::phase_fits(phase, period);
    }

    explicit ReplayFeeder(Wiring w) noexcept;
    ~ReplayFeeder();
    ReplayFeeder(const ReplayFeeder&) = delete;
    ReplayFeeder& operator=(const ReplayFeeder&) = delete;

    bool take_play(const Play& p) noexcept;
    bool take_stop() noexcept;

    void tick() noexcept;

    [[nodiscard]] Stage stage() const noexcept { return stage_; }

    [[nodiscard]] ControlGeneration gen() const noexcept { return play_.run.gen; }

    [[nodiscard]] Refusal last_refusal() const noexcept { return last_refusal_; }

    [[nodiscard]] std::uint32_t last_refusal_detail() const noexcept { return last_detail_; }

    [[nodiscard]] std::string_view last_setting() const noexcept {
        return play_.report.last_setting;
    }

    [[nodiscard]] bool last_setting_offered() const noexcept {
        return play_.report.last_setting_offered;
    }

    [[nodiscard]] std::string_view refusal_path() const noexcept {
        return play_.report.refusal_path;
    }

    [[nodiscard]] std::uint32_t refusals() const noexcept { return refusals_; }

    [[nodiscard]] std::uint32_t scanned() const noexcept { return play_.scan.scanned; }

    [[nodiscard]] FrameCount movie_frames() const noexcept { return play_.scan.movie_frames; }

    [[nodiscard]] std::uint32_t tries() const noexcept { return play_.run.tries; }

    [[nodiscard]] std::span<const SetSetting> settings_set() const noexcept {
        return play_.report.set;
    }

private:
    [[nodiscard]] static std::unique_ptr<IReplayStage> make_stage_(IReplayStage::Step::Next next);

    [[nodiscard]] IReplayStage::Ctx ctx_() noexcept {
        return {w_, play_.request, play_.scan, play_.arm, play_.report, play_.run};
    }

    void apply_(IReplayStage::Step step) noexcept;
    void drive_(IReplayStage::Step step) noexcept;
    void refuse_(PlayRefusal why) noexcept;

    Wiring w_;
    ReplayPlay play_{};
    std::unique_ptr<IReplayStage> active_{};
    Stage stage_ = Stage::Idle;
    Refusal last_refusal_ = Refusal::None;
    std::uint32_t last_detail_ = 0;
    std::uint32_t refusals_ = 0;
};

}
