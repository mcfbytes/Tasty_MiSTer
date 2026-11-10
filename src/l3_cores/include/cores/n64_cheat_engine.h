// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/cheat_sink.h"
#include "cores/core_window.h"
#include "cores/n64_cheat_code.h"
#include "cores/n64_db_tags.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "cores/n64_ntsc_cadence.h"

namespace mister::cores::n64 {

enum class CheatTick : std::uint8_t {
    Idle,
    Ran,
    Invalid,
};

struct CheatGates {
    bool loaded = false;
    SystemType system = SystemType::Ntsc;
    bool enabled = false;
    bool gs_pressed = false;
};

class CheatEngine final : public ICheatSink {
    TASTY_SEAT_RESIDENT(RT);

public:
    CheatEngine(const os::IClock& clock, infra::OptRef<ICoreWindow> window) noexcept
        : clock_(clock),
          window_(window) {}

    static constexpr std::chrono::milliseconds kNoGameGap{1000};

    static constexpr std::chrono::milliseconds kResetGap{2500};

    static constexpr std::chrono::milliseconds kMaxLag{100};

    [[nodiscard]] Ex<void> apply(std::span<const std::uint8_t> table, CheatUnit unit) override;

    [[nodiscard]] CheatTick tick(const CheatGates& gates) noexcept;

    void rearm_boot_codes() noexcept;

    [[nodiscard]] std::uint32_t count() const noexcept;
    [[nodiscard]] std::uint32_t applies() const noexcept;
    [[nodiscard]] std::uint32_t passes() const noexcept;
    [[nodiscard]] std::uint32_t code_writes() const noexcept;
    [[nodiscard]] std::uint32_t invalids() const noexcept;
    [[nodiscard]] std::uint32_t window_refusals() const noexcept;
    [[nodiscard]] std::uint32_t wire_faults() const noexcept;
    [[nodiscard]] std::uint32_t dropped_records() const noexcept;

private:
    enum class CodeOutcome : std::uint8_t { Done, Unfulfilled, Invalid };

    [[nodiscard]] CodeOutcome execute_(CheatCode& c, bool gs_pressed) noexcept;

    [[nodiscard]] static CodeOutcome compare_(
        std::span<const std::uint8_t> mem,
        const CheatCode& c
    ) noexcept;
    [[nodiscard]] bool write_back_(const CheatCode& c) noexcept;

    const os::IClock& clock_;
    infra::OptRef<ICoreWindow> window_;
    std::array<CheatCode, CheatCode::kMax> codes_{};
    std::uint32_t count_ = 0;

    std::optional<os::RawMonotonicClock::time_point> due_{};
    NtscCadence cadence_{};
    std::uint32_t applies_ = 0;
    std::uint32_t passes_ = 0;
    std::uint32_t code_writes_ = 0;
    std::uint32_t invalids_ = 0;
    std::uint32_t window_refusals_ = 0;
    std::uint32_t wire_faults_ = 0;
    std::uint32_t dropped_records_ = 0;
};

}
