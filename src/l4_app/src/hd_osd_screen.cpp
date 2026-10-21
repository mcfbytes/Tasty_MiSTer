// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hd_osd_screen.h"

#include "app/hps_framebuffer.h"
#include "app/video_pump.h"

namespace mister::app {

void HdOsdScreen::publish_ask_(bool is_open) noexcept {
    HdOsdAsk ask{};
    ask.open = is_open;
    ask.epoch = epoch_;
    if (is_open) {
        ask.surface = surface_;
        ask.video_applies = applies_seen_;
        ask.page = page_;
        ask.badge = badge_;
        page_sent_ = page_;
        badge_sent_ = badge_;
    }
    const auto ns = w_.clock.now().count();
    ask.stamp_ns = ns < 0 ? 0 : static_cast<std::uint64_t>(ns);
    w_.ask.publish(ask);
    w_.wake.kick_if_armed();
}

void HdOsdScreen::retry_drop_() noexcept {
    if (!drop_owed_)
        return;
    if (w_.fb.raised()) {
        drop_owed_ = false;
        ours_ = false;
        shown_.reset();
        sync_console_();
        return;
    }
    if (!w_.fb.drop())
        return;
    ours_ = false;
    drop_owed_ = false;
    shown_.reset();
    sync_console_();
}

void HdOsdScreen::close_() noexcept {
    if (open_) {
        open_ = false;
        publish_ask_(false);
    }
    badge_ = HdOsdStatus::Badge::Unknown;
    reraise_owed_ = false;
    owed_.reset();
    covers_ = false;
    intends_ = false;
    if (!ours_) {
        drop_owed_ = false;
        shown_.reset();
    } else if (w_.fb.drop()) {
        ours_ = false;
        drop_owed_ = false;
        shown_.reset();
    } else {
        drop_owed_ = true;
    }
    while (w_.flips.take()) {
    }
    sync_console_();
}

void HdOsdScreen::sync_console_() noexcept {
    if (ours_) {
        capture_console_();
    } else {
        release_console_();
    }
}

void HdOsdScreen::capture_console_() noexcept {
    if (!w_.vt || guarded_vt_ != 0)
        return;
    const auto vt = w_.vt->active();
    if (!vt)
        return;
    const auto mode = w_.vt->text_mode(*vt);
    if (!mode || !*mode)
        return;
    w_.vt->graphics(*vt);
    guarded_vt_ = *vt;
}

void HdOsdScreen::release_console_() noexcept {
    if (guarded_vt_ == 0)
        return;
    if (w_.vt)
        w_.vt->text(guarded_vt_);
    guarded_vt_ = 0;
}

bool HdOsdScreen::try_open_() noexcept {
    const std::uint32_t ow = w_.video.output_width();
    const std::uint32_t oh = w_.video.output_height();
    const auto fit = fit_surface(w_.fb.region_bytes(), ow, oh);
    if (!fit)
        return false;
    surface_ = *fit;
    out_w_ = ow;
    out_h_ = oh;
    applies_seen_ = w_.video.stats().applies;
    ++epoch_;
    open_ = true;
    intends_ = true;
    publish_ask_(true);
    return true;
}

bool HdOsdScreen::reraise_() noexcept {
    if (!shown_)
        return false;
    const auto raised = w_.fb.raise_view((*shown_)->view);
    if (w_.fb.raised())
        ours_ = true;
    sync_console_();
    if (!raised)
        return false;
    covers_ = true;
    intends_ = true;
    return true;
}

void HdOsdScreen::on_applies_() noexcept {
    const std::uint32_t applies = w_.video.stats().applies;
    if (applies == applies_seen_)
        return;
    const std::uint32_t ow = w_.video.output_width();
    const std::uint32_t oh = w_.video.output_height();
    const bool sized = ow != out_w_ || oh != out_h_;
    covers_ = false;
    intends_ = false;
    applies_seen_ = applies;
    if (!sized) {
        publish_ask_(true);
        if (shown_)
            reraise_owed_ = !reraise_();
        return;
    }
    const auto fit = fit_surface(w_.fb.region_bytes(), ow, oh);
    if (!fit) {
        close_();
        return;
    }
    surface_ = *fit;
    out_w_ = ow;
    out_h_ = oh;
    ++epoch_;
    shown_.reset();
    owed_.reset();
    reraise_owed_ = false;
    publish_ask_(true);
}

void HdOsdScreen::raise_job_(HdFlipChannel::Job job) noexcept {
    reraise_owed_ = false;
    if (w_.fb.held()) {
        owed_ = std::move(job);
        return;
    }
    const auto raised = w_.fb.raise_view(job->view);
    if (w_.fb.raised())
        ours_ = true;
    if (!raised) {
        owed_ = std::move(job);
        sync_console_();
        return;
    }
    drop_owed_ = false;
    covers_ = true;
    intends_ = true;
    shown_ = std::move(job);
    sync_console_();
}

void HdOsdScreen::take_jobs_() noexcept {
    if (owed_) {
        auto job = std::move(*owed_);
        owed_.reset();
        if (!job || job->epoch != epoch_) {
            if (job)
                ++stale_drops_;
        } else {
            raise_job_(std::move(job));
        }
    }
    for (;;) {
        auto job = w_.flips.take();
        if (!job)
            break;
        if (job->epoch != epoch_) {
            ++stale_drops_;
            continue;
        }
        raise_job_(std::move(job));
    }
}

void HdOsdScreen::tick(const PageDescription* page, HdOsdStatus::Badge badge) noexcept {
    TASTY_SEAT_BODY(HdOsdScreen);
    retry_drop_();
    (void)status_seen_.take_if_changed(w_.status, status_);
    const bool live = status_.state == HdOsdStatus::State::Ready
        || status_.state == HdOsdStatus::State::Showing;
    if (page == nullptr || !live) {
        close_();
        return;
    }
    page_ = *page;
    badge_ = badge;
    if (open_ && w_.fb.held()) {
        close_();
        return;
    }
    if (open_)
        on_applies_();
    if (!open_) {
        if (available())
            (void)try_open_();
    } else if (page_ != page_sent_ || badge_ != badge_sent_) {
        publish_ask_(true);
    }
    if (open_)
        take_jobs_();
    if (reraise_owed_)
        reraise_owed_ = !reraise_();
}

bool HdOsdScreen::intends_hdmi() const noexcept {
    TASTY_SEAT_BODY(HdOsdScreen);
    return intends_;
}

bool HdOsdScreen::covers_hdmi() const noexcept {
    TASTY_SEAT_BODY(HdOsdScreen);
    return covers_;
}

void HdOsdScreen::release_console() noexcept {
    TASTY_SEAT_BODY(HdOsdScreen);
    release_console_();
}

bool HdOsdScreen::available() const noexcept {
    TASTY_SEAT_BODY(HdOsdScreen);
    if (status_.state != HdOsdStatus::State::Ready && status_.state != HdOsdStatus::State::Showing)
        return false;
    if (w_.fb.held())
        return false;
    return !w_.fb.raised() || ours_;
}

std::uint32_t HdOsdScreen::stale_drops() const noexcept {
    TASTY_SEAT_BODY(HdOsdScreen);
    return stale_drops_;
}

}
