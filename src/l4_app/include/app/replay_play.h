// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "app/path_text.h"
#include "app/replay_msg.h"
#include "app/replay_status.h"
#include "app/types.h"
#include "cores/movie_codec.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::xthread {
class DiagLog;
}

namespace mister::app {

struct ReplayPlay {
    TASTY_SEAT_RESIDENT(Ui);

    struct SetSetting {
        std::string_view name{};
        std::string was{};
        std::string now{};
    };

    static constexpr std::uint32_t kMaxFrames = 1u << 20;

    using ArmMicros = std::chrono::duration<std::uint32_t, std::micro>;
    static constexpr std::chrono::microseconds kPhaseMin{1000};
    static constexpr std::chrono::microseconds kPhaseGuard{2000};

    [[nodiscard]] static constexpr bool phase_fits(
        std::chrono::microseconds phase,
        std::chrono::nanoseconds period
    ) noexcept {
        const auto period_us = std::chrono::duration_cast<std::chrono::microseconds>(period);
        return period_us > kPhaseGuard && phase >= kPhaseMin && phase <= period_us - kPhaseGuard;
    }

    static constexpr FrameCount kRunSplit{60};
    static constexpr std::uint32_t kReadChunk = 16 * 1024;
    static constexpr std::uint32_t kTickBudget = 64 * 1024;
    static constexpr unsigned kEpochTries = 3;
    static constexpr std::size_t kCheckpoints = 64;
    static constexpr std::int64_t kAnswerNs = 2'000'000'000;
    static constexpr std::int64_t kPowerOnNs = 20'000'000'000;
    static constexpr std::int64_t kDiscSettleNs = 2'000'000'000;

    void log_end(xthread::DiagLog& diag, const ReplayStatus& s) const noexcept;

    void release_movie() noexcept { runs = {}; }

    ControlGeneration gen{};
    infra::OptRef<const cores::IMovieCodec> codec{};
    cores::IMovieCodec::Facts facts{};
    PathText movie{};
    PathText rom{};
    std::uint32_t offset_us = 0;
    std::optional<std::int32_t> lead_override{};
    std::optional<MovieFrame> stop_at{};
    std::optional<cores::IMovieCodec::RamFill> ram_fill{};
    bool seeded_save = false;
    bool set_ok = false;
    std::vector<SetSetting> set{};
    std::int32_t lead = 0;
    std::uint32_t poweron_ns = 0;
    ReplayMsg::P0Parity p0 = ReplayMsg::P0Parity::Any;
    ReplayMsg::PowerOnEvent event = ReplayMsg::PowerOnEvent::LoadEnd;
    bool vsync_ok = false;
    std::uint8_t rom_index = 0xFF;

    std::uint32_t scanned = 0;
    FrameCount pushed{};
    FrameCount movie_frames{};

    std::vector<ReplayMsg::Input> runs{};
    unsigned tries = 0;

    std::array<MovieFrame, kCheckpoints> checks{};
    std::size_t n_checks = 0;
    std::size_t next_check = 0;

    std::string_view last_setting{};
    bool last_setting_offered = false;
    std::string refusal_path{};
};

}
