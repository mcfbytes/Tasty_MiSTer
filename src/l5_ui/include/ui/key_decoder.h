// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/seat.h"
#include "proto/types.h"
#include "ui/ui_event.h"

namespace mister::os {
class IClock;
}

namespace mister::ui {

class KeyDecoder {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit KeyDecoder(const os::IClock& clock) noexcept : clock_(&clock) {}

    static constexpr std::uint32_t kUpstroke = 0x400000u;

    void push_raw(proto::KeyCode code) noexcept;

    void push_synth(proto::KeyCode code) noexcept;

    void reset() noexcept;

    std::optional<UiEvent> pop() noexcept;

    void set_ini_select_mode(bool on) noexcept { ini_select_ = on; }

    bool ini_select_mode() const noexcept { return ini_select_; }

    void set_repeat_all(bool on) noexcept { repeat_all_ = on; }

    bool repeat_all() const noexcept { return repeat_all_; }

    bool pending() const noexcept { return count_ != 0 || db_pending_ != 0; }

    std::uint32_t dropped() const noexcept { return dropped_; }

    std::uint16_t repeat_depth() const noexcept { return hold_cnt_; }

    static std::uint16_t decode(std::uint32_t code, bool ini_select) noexcept;

    static bool repeatable(std::uint32_t code) noexcept;

private:
    static constexpr unsigned kQueue = 32;

    void enqueue(proto::KeyCode code, bool synth) noexcept;

    std::optional<UiEvent> deliver_(std::uint32_t code, std::int64_t now, bool first) noexcept;

    std::optional<UiEvent> accept_(std::uint32_t code, std::int64_t now) noexcept;

    const os::IClock* clock_;
    std::uint32_t slots_[kQueue]{};
    bool synth_[kQueue]{};
    unsigned head_ = 0;
    unsigned count_ = 0;
    bool ini_select_ = false;
    bool repeat_all_ = false;
    std::uint32_t dropped_ = 0;

    std::uint32_t last_seen_ = 0;
    std::uint32_t db_pending_ = 0;
    std::int64_t db_at_ns_ = 0;
    std::int64_t repeat_at_ns_ = 0;
    std::uint16_t hold_cnt_ = 0;
};

}
