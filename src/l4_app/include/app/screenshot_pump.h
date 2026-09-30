// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "app/event_sink.h"
#include "app/screenshot_queue.h"
#include "infra/seat.h"

namespace mister::app {

class ScreenshotPump {
    TASTY_SEAT_RESIDENT(Ui);

public:
    void bind_queue(ScreenshotQueue* q) noexcept { shots_ = q; }
    void set_event_sink(IEventSink* sink) noexcept { sink_ = sink; }

    [[nodiscard]] bool take(std::string_view path, bool scaled) noexcept;
    void tick() noexcept;

    std::uint32_t armed() const noexcept {
        TASTY_SEAT_BODY(ScreenshotPump);
        return armed_;
    }
    std::uint32_t reaped() const noexcept {
        TASTY_SEAT_BODY(ScreenshotPump);
        return reaped_;
    }

private:
    ScreenshotQueue* shots_ = nullptr;
    IEventSink* sink_ = nullptr;
    std::uint32_t armed_ = 0;
    std::uint32_t reaped_ = 0;
};

}  // namespace mister::app
