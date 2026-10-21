// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "cores/cheat_sink.h"
#include "cores/core_window.h"
#include "cores/n64_cheat_code.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "os/clock.h"

namespace mister::cores::n64 {

enum class CheatTick : std::uint8_t {
    Idle,
    Ran,
    Invalid,
};

class CheatEngine final : public ICheatSink {
    TASTY_SEAT_RESIDENT(RT);

public:
    CheatEngine(const os::IClock& clock, ICoreWindow* window) noexcept
        : clock_(&clock),
          window_(window) {}

    static constexpr std::int64_t kPalGapMs = 20;
    static constexpr std::int64_t kNoGameGapMs = 1000;

    static constexpr std::int64_t kResetGapMs = 2500;

    static constexpr std::int64_t kMaxLagMs = 100;

    [[nodiscard]] Ex<void> apply(std::span<const std::uint8_t> table, std::uint32_t unit) override;

    [[nodiscard]] CheatTick tick(bool loaded, bool pal, bool enabled, bool gs_pressed) noexcept;

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
    [[nodiscard]] int execute_(CheatCode& c, bool gs_pressed) noexcept;

    [[nodiscard]] static int compare_(
        std::span<const std::uint8_t> mem,
        const CheatCode& c
    ) noexcept;
    [[nodiscard]] bool write_back_(const CheatCode& c) noexcept;

    const os::IClock* clock_;
    ICoreWindow* window_;
    std::array<CheatCode, CheatCode::kMax> codes_{};
    std::uint32_t count_ = 0;
    std::int64_t due_ns_ = 0;
    std::uint8_t adj_ = 0;
    std::uint32_t applies_ = 0;
    std::uint32_t passes_ = 0;
    std::uint32_t code_writes_ = 0;
    std::uint32_t invalids_ = 0;
    std::uint32_t window_refusals_ = 0;
    std::uint32_t wire_faults_ = 0;
    std::uint32_t dropped_records_ = 0;
};

}
