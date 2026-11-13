// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>

#include "infra/error.h"
#include "infra/wake_flag.h"
#include "hal/types.h"
#include "reactor/frame_record.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "os/framebuffer.h"

namespace mister::reactor {

class FrameClock {
    TASTY_SEAT_RESIDENT(Frame);

public:
    struct Wiring {
        const os::IClock& clock;
        std::optional<xthread::WakeFlag::Kicker> round_wake{};
    };

    static Ex<std::unique_ptr<FrameClock>> open(const char* fbdev_path, const Wiring& wiring);

    [[nodiscard]] const FrameCell& frame_cell() const noexcept { return cell_; }

    void wait_vsync() noexcept;

private:
    explicit FrameClock(const Wiring& w) noexcept : clock_(w.clock), round_wake_(w.round_wake) {}

    const os::IClock& clock_;

    hal::FrameSeq seq_{};
    FrameCell cell_{};
    os::Framebuffer fb_;
    std::optional<xthread::WakeFlag::Kicker> round_wake_{};
};

}
