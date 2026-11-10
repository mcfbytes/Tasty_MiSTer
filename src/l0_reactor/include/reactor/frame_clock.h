// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include "infra/error.h"
#include "infra/unique_fd.h"
#include "infra/wake_flag.h"
#include "hal/types.h"
#include "reactor/frame_record.h"
#include "infra/seat.h"
#include "os/framebuffer.h"

namespace mister::reactor {

class FrameClock {
    TASTY_SEAT_RESIDENT(Frame);

public:
    static Ex<std::unique_ptr<FrameClock>> open(const char* fbdev_path);

    int eventfd() const noexcept { return frame_wake_.fd(); }

    [[nodiscard]] const FrameCell& frame_cell() const noexcept { return cell_; }

    void wait_vsync() noexcept;

private:
    FrameClock() = default;

    hal::FrameSeq seq_{};
    FrameCell cell_{};
    os::Framebuffer fb_;
    xthread::WakeFlag frame_wake_{};
};

}
