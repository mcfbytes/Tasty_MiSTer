// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/launcher_screen.h"

#include "app/hps_framebuffer.h"

namespace mister::app {

bool LauncherScreen::raise_() noexcept {
    const auto lease = w_.fb.raise(FbTenant::Launcher);
    if (!lease)
        return false;
    lease_ = *lease;
    drop_owed_ = false;
    ++raises_;
    return true;
}

void LauncherScreen::drop_lease_() noexcept {
    if (!lease_)
        return;
    if (!w_.fb.drop(*lease_) && w_.fb.holds(*lease_)) {
        drop_owed_ = true;
        return;
    }
    drop_owed_ = false;
    lease_.reset();
}

void LauncherScreen::retry_drop_() noexcept {
    if (drop_owed_)
        drop_lease_();
}

void LauncherScreen::tick() noexcept {
    TASTY_SEAT_BODY(LauncherScreen);
    retry_drop_();
    LauncherState s{};
    const std::uint32_t applies = w_.video_stats.sample().value.applies;
    if (seen_.take_if_changed(w_.state, s)) {
        w_.fb.hold(s.scanout_owned);
        const bool new_claim = s.owns_screen
            && (!last_.owns_screen || s.screen_gen != last_.screen_gen);
        if (new_claim) {
            raise_owed_ = true;
        } else if (!s.owns_screen && last_.owns_screen) {
            raise_owed_ = false;
            drop_lease_();
        } else if (s.owns_screen && last_.scanout_owned && !s.scanout_owned) {
            raise_owed_ = true;
        }
        if (s.scanout_owned != last_.scanout_owned)
            ack_owed_ = true;
        if (s.settled && !last_.settled)
            w_.osd.close_osd();
        last_ = s;
    } else if (last_.owns_screen && applies != applies_seen_) {
        raise_owed_ = true;
    }
    applies_seen_ = applies;

    if (last_.owns_screen && !last_.scanout_owned && !w_.fb.raised())
        raise_owed_ = true;

    if (raise_owed_ && last_.owns_screen && raise_()) {
        raise_owed_ = false;
        acked_gen_ = last_.screen_gen;
        ack_owed_ = true;
    }
    if (!ack_owed_)
        return;
    ack_owed_ = false;
    w_.fb_ack.publish(LauncherFbAck{acked_gen_, last_.scanout_owned});
    w_.launcher_wake.kick_if_armed();
}

}
