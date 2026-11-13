// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/front_end_idle_clock.h"

namespace mister::ui {

using namespace mister;

FrontEndIdleClock::Result FrontEndIdleClock::round(const Inputs& in) noexcept {
    TASTY_SEAT_BODY(FrontEndIdleClock);

    const bool any_key = in.any_key || wake_seen_;
    wake_seen_ = false;
    offered_.reset();

    Result out;
    if (owed_) {
        out.ctl = owed_;
        offered_ = owed_;
        owed_.reset();
    }

    const bool active = in.front_end
        && in.on_front_page
        && in.knobs_published
        && in.knobs.osd_timeout >= kOsdTimeoutMin
        && in.knobs.fb_terminal;
    if (!active) {
        if (idle_ != app::FrontEndIdle::Awake)
            wake_(out);
        deadline_ = std::chrono::nanoseconds::zero();
        off_deadline_ = std::chrono::nanoseconds::zero();
        release_owed_ = false;
        out.idle = idle_;
        return out;
    }

    if (any_key) {
        deadline_ = std::chrono::nanoseconds::zero();
        if (idle_ != app::FrontEndIdle::Awake)
            wake_(out);
    }

    if (deadline_ != std::chrono::nanoseconds::zero() && in.now >= deadline_) {
        deadline_ = std::chrono::nanoseconds::zero();
        switch (idle_) {
            case app::FrontEndIdle::Awake:
                idle_ = app::FrontEndIdle::NoLogo;
                out.ctl = false;
                offered_ = false;
                break;
            case app::FrontEndIdle::NoLogo:
                idle_ = app::FrontEndIdle::Dimmed;
                if (in.knobs.video_off > std::chrono::seconds::zero())
                    off_deadline_ = in.now + in.knobs.video_off;
                break;

            case app::FrontEndIdle::Dimmed:
            case app::FrontEndIdle::Dark:
            case app::FrontEndIdle::DarkLogo:
                break;
        }
    }

    if (off_deadline_ != std::chrono::nanoseconds::zero() && in.now >= off_deadline_) {
        off_deadline_ = std::chrono::nanoseconds::zero();
        if (idle_ == app::FrontEndIdle::Dimmed) {
            idle_ = in.knobs.video_off_logo ? app::FrontEndIdle::DarkLogo : app::FrontEndIdle::Dark;
            if (in.knobs.video_off_logo)
                off_deadline_ = in.now + kDarkLogoRearm;
        } else if (idle_ == app::FrontEndIdle::DarkLogo) {

            rearm_.add();
            off_deadline_ = in.now + kDarkLogoRearm;
        }
    }

    if (deadline_ == std::chrono::nanoseconds::zero())
        deadline_ = in.now + in.knobs.osd_timeout;
    out.idle = idle_;
    return out;
}

void FrontEndIdleClock::ctl_pushed(bool landed) noexcept {
    TASTY_SEAT_BODY(FrontEndIdleClock);
    if (landed) {
        ctls_.add();
        offered_.reset();
        return;
    }
    refusals_.add();
    owed_ = offered_;
}

bool FrontEndIdleClock::on_key(const app::RawKeyEdge& edge) noexcept {
    TASTY_SEAT_BODY(FrontEndIdleClock);
    if (!edge.pressed) {
        if (release_owed_ && edge.code == release_code_) {
            release_owed_ = false;
            return true;
        }
        return false;
    }
    if (idle_ == app::FrontEndIdle::Awake || wake_seen_)
        return false;

    release_code_ = edge.code;
    release_owed_ = true;
    wake_seen_ = true;
    return true;
}

void FrontEndIdleClock::wake_(Result& out) noexcept {
    idle_ = app::FrontEndIdle::Awake;
    deadline_ = std::chrono::nanoseconds::zero();
    off_deadline_ = std::chrono::nanoseconds::zero();
    out.ctl = true;
    offered_ = true;
}

}
