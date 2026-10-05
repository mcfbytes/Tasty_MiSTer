// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/launcher_screen.h"

#include "app/hps_framebuffer.h"
#include "app/video_pump.h"

namespace mister::app {

bool LauncherScreen::raise_() noexcept {
    if (w_.fb == nullptr) return true;
    if (!w_.fb->raise()) return false;
    ++raises_;
    return true;
}

void LauncherScreen::tick() noexcept {
    TASTY_SEAT_BODY(LauncherScreen);
    if (w_.state == nullptr) return;
    LauncherState s{};
    const std::uint32_t applies = w_.video != nullptr ? w_.video->stats().applies : 0u;
    if (seen_.take_if_changed(*w_.state, s)) {
        if (w_.fb != nullptr) w_.fb->hold(s.scanout_owned);
        const bool new_claim =
            s.owns_screen && (!last_.owns_screen || s.screen_gen != last_.screen_gen);
        if (new_claim) {
            raise_owed_ = true;
        } else if (!s.owns_screen && last_.owns_screen) {
            raise_owed_ = false;
            if (w_.fb != nullptr) (void)w_.fb->drop();
        } else if (s.owns_screen && last_.scanout_owned && !s.scanout_owned) {
            raise_owed_ = true;
        }
        if (s.scanout_owned != last_.scanout_owned) ack_owed_ = true;
        if (s.settled && !last_.settled && w_.osd != nullptr) w_.osd->close_osd();
        last_ = s;
    } else if (last_.owns_screen && applies != applies_seen_) {
        raise_owed_ = true;
    }
    applies_seen_ = applies;

    if (raise_owed_ && last_.owns_screen && raise_()) {
        raise_owed_ = false;
        acked_gen_ = last_.screen_gen;
        ack_owed_ = true;
    }
    if (!ack_owed_) return;
    ack_owed_ = false;
    if (w_.fb_ack != nullptr) w_.fb_ack->publish(LauncherFbAck{acked_gen_, last_.scanout_owned});
    if (w_.launcher_wake != nullptr) w_.launcher_wake->kick_if_armed();
}

}  // namespace mister::app
