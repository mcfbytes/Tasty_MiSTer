// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "app/avi_writer.h"
#include "app/sidecar_writer.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "infra/seat_main.h"
#include "infra/seat_park.h"
#include "infra/wake_flag.h"

namespace mister::app {

class RecWriteMain final : public xthread::SeatMain<RecWriteMain> {
    TASTY_SEAT_RESIDENT(RecWrite);

public:
    RecWriteMain(SidecarWriter& writer, AviWriter& avi, xthread::WakeFlag& wake) noexcept
        : writer_(writer), avi_(avi), wake_(wake) {}
    RecWriteMain(const RecWriteMain&) = delete;
    RecWriteMain& operator=(const RecWriteMain&) = delete;
    RecWriteMain(RecWriteMain&&) = delete;
    RecWriteMain& operator=(RecWriteMain&&) = delete;

    [[nodiscard]] Ex<void> open() noexcept;

    void start() noexcept;
    void stop() noexcept { stop_.request(); }

    void serve() noexcept;

    [[nodiscard]] bool idle() const noexcept { return writer_.idle() && avi_.idle(); }
    [[nodiscard]] bool stopping() const noexcept { return stop_.ever_requested(); }
    [[nodiscard]] int park_ms() const noexcept;
    [[nodiscard]] xthread::SeatPark& park() noexcept { return *park_; }

    [[nodiscard]] bool paused() noexcept { return false; }
    [[nodiscard]] bool pause_pending() const noexcept { return false; }
    void settle() noexcept {
        avi_.release();
        writer_.release();
    }

private:
    SidecarWriter& writer_;
    AviWriter& avi_;
    xthread::WakeFlag& wake_;
    xthread::WakeFlag stop_{};
    std::optional<xthread::SeatPark> park_{};
};

static_assert(xthread::SeatBody<RecWriteMain>);

}  // namespace mister::app
