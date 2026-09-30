// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/screenshot_pump.h"

#include "app/event.h"
#include "infra/error.h"

namespace mister::app {

bool ScreenshotPump::take(std::string_view path, bool scaled) noexcept {
    TASTY_SEAT_BODY(ScreenshotPump);
    if (shots_ == nullptr) return false;
    auto shot = shots_->arm();

    if (!shot) return true;
    (void)shot->path.assign(path);
    shot->scaled = scaled ? 1 : 0;
    shot->ok = 0;
    shots_->submit(std::move(shot));
    ++armed_;
    return true;
}

void ScreenshotPump::tick() noexcept {
    TASTY_SEAT_BODY(ScreenshotPump);
    if (shots_ == nullptr) return;
    while (auto res = shots_->reap()) {

        const bool wrote = res.completed() && res->ok != 0;
        ++reaped_;
        if (sink_ == nullptr) continue;
        sink_->deliver(infra::make<Event>(
            Event::InfoRequest{.id = wrote ? InfoId::ScreenshotWritten : InfoId::ScreenshotFailed},
            Event::Head{EmitSite{ERR_SITE()}, {}, res->tag}));
    }
}

}  // namespace mister::app
