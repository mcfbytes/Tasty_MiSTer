// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "app/bay_counts.h"
#include "hal/enable_word.h"
#include "hal/entry_mask.h"
#include "hal/reg_block.h"
#include "hal/reg_port.h"
#include "hal/sys_directory.h"
#include "hal/window_port.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/seat_owner_main.h"
#include "os/clock.h"
#include "os/delay.h"
#include "os/nanosleep_delay.h"
#include "proto/block_line.h"
#include "proto/dir_entry_kind.h"

namespace mister::app {

class LwHostEnable {
    TASTY_SEAT_RESIDENT(Main);

public:
    static constexpr std::chrono::microseconds kQuietBound{50'000};
    static constexpr std::chrono::microseconds kOpenBound{50'000};
    static constexpr std::chrono::microseconds kPollGap{100};

    LwHostEnable(
        BayCounts& counts,
        BayCountsCell& cell,
        const os::IClock& clock,
        infra::OptRef<os::IDelay> delay = {}
    ) noexcept;
    LwHostEnable(const LwHostEnable&) = delete;
    LwHostEnable& operator=(const LwHostEnable&) = delete;

    void attach(hal::reg::Lend header) noexcept;
    void attach(hal::IRegPort<hal::SysDirectory>& port) noexcept;

    void attach_unmapped() noexcept;

    void drop() noexcept;

    void clear_masks() noexcept;

    void note_grant(
        proto::DirEntryKind kind,
        hal::SysDirectory::EntryIndex at,
        bool carries_line
    ) noexcept;

    [[nodiscard]] bool attached() const noexcept { return port_.has_value() || unmapped_; }

    [[nodiscard]] bool served() const noexcept { return attached() && any_; }

    [[nodiscard]] hal::EntryMask served_linked() const noexcept { return linked_; }

    [[nodiscard]] hal::EntryMask served_lines() const noexcept { return lines_; }

    [[nodiscard]] Ex<void> take() noexcept;

    void arm_lines() noexcept;

    [[nodiscard]] Ex<void> confirm() noexcept;

private:
    [[nodiscard]] hal::EnableWord enable_word_() noexcept;
    [[nodiscard]] bool poll_quiet_() noexcept;
    [[nodiscard]] bool poll_open_() noexcept;
    [[nodiscard]] std::chrono::microseconds since_(
        os::RawMonotonicClock::time_point start
    ) const noexcept;
    [[nodiscard]] Ex<void> refuse_(Errc why, hal::RegWord detail) noexcept;

    [[nodiscard]] os::IDelay& delay_ref_() noexcept { return delay_ ? *delay_ : own_delay_; }

    void publish_() noexcept;

    BayCounts& counts_;
    BayCountsCell& cell_;
    const os::IClock& clock_;
    os::NanosleepDelay own_delay_{};
    infra::OptRef<os::IDelay> delay_{};

    std::optional<hal::WindowPort<hal::SysDirectory, seat_owner::Main>> window_{};
    infra::OptRef<hal::IRegPort<hal::SysDirectory>> port_{};
    hal::EntryMask linked_{};
    hal::EntryMask lines_{};
    bool any_ = false;
    bool unmapped_ = false;
};

}

namespace mister::hal {

extern template class WindowPort<SysDirectory, seat_owner::Main>;
}
