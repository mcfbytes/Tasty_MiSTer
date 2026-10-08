// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hd_osd_screen.h"

#include "app/hps_framebuffer.h"
#include "app/video_pump.h"

namespace mister::app {

void HdOsdScreen::publish_ask_(bool is_open) noexcept {
    if (w_.ask == nullptr) return;
    HdOsdAsk ask{};
    ask.open = is_open;
    ask.epoch = epoch_;
    if (is_open) {
        ask.surface = surface_;
        ask.video_applies = applies_seen_;
        ask.page = page_;
        page_sent_ = page_;
    }
    w_.ask->publish(ask);
    if (w_.wake != nullptr) w_.wake->kick_if_armed();
}

void HdOsdScreen::retry_drop_() noexcept {
    if (!drop_owed_ || w_.fb == nullptr) return;
    if (w_.fb->raised()) {
        drop_owed_ = false;
        ours_ = false;
        shown_.reset();
        return;
    }
    if (!w_.fb->drop()) return;
    ours_ = false;
    drop_owed_ = false;
    shown_.reset();
}

void HdOsdScreen::close_() noexcept {
    if (open_) {
        open_ = false;
        publish_ask_(false);
    }
    reraise_owed_ = false;
    owed_.reset();
    covers_ = false;
    if (!ours_ || w_.fb == nullptr) {
        drop_owed_ = false;
        shown_.reset();
    } else if (w_.fb->drop()) {
        ours_ = false;
        drop_owed_ = false;
        shown_.reset();
    } else {
        drop_owed_ = true;
    }
    if (w_.flips == nullptr) return;
    while (w_.flips->take()) {
    }
}

bool HdOsdScreen::try_open_() noexcept {
    if (w_.fb == nullptr || w_.video == nullptr) return false;
    const std::uint32_t ow = w_.video->output_width();
    const std::uint32_t oh = w_.video->output_height();
    const auto fit = fit_surface(w_.fb->region_bytes(), ow, oh);
    if (!fit) return false;
    surface_ = *fit;
    out_w_ = ow;
    out_h_ = oh;
    applies_seen_ = w_.video->stats().applies;
    ++epoch_;
    open_ = true;
    publish_ask_(true);
    return true;
}

bool HdOsdScreen::reraise_() noexcept {
    if (!shown_ || w_.fb == nullptr) return false;
    const auto raised = w_.fb->raise_view((*shown_)->view);
    if (w_.fb->raised()) ours_ = true;
    if (!raised) return false;
    covers_ = true;
    return true;
}

void HdOsdScreen::on_applies_() noexcept {
    if (w_.video == nullptr) return;
    const std::uint32_t applies = w_.video->stats().applies;
    if (applies == applies_seen_) return;
    const std::uint32_t ow = w_.video->output_width();
    const std::uint32_t oh = w_.video->output_height();
    const bool sized = ow != out_w_ || oh != out_h_;
    covers_ = false;
    applies_seen_ = applies;
    if (!sized) {
        publish_ask_(true);
        if (shown_) reraise_owed_ = !reraise_();
        return;
    }
    if (w_.fb == nullptr) {
        close_();
        return;
    }
    const auto fit = fit_surface(w_.fb->region_bytes(), ow, oh);
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
    if (w_.fb == nullptr) {
        owed_ = std::move(job);
        return;
    }
    if (w_.fb->held()) {
        owed_ = std::move(job);
        return;
    }
    const auto raised = w_.fb->raise_view(job->view);
    if (w_.fb->raised()) ours_ = true;
    if (!raised) {
        owed_ = std::move(job);
        return;
    }
    drop_owed_ = false;
    covers_ = true;
    shown_ = std::move(job);
}

void HdOsdScreen::take_jobs_() noexcept {
    if (owed_) {
        auto job = std::move(*owed_);
        owed_.reset();
        if (!job || job->epoch != epoch_) {
            if (job) ++stale_drops_;
        } else {
            raise_job_(std::move(job));
        }
    }
    if (w_.flips == nullptr) return;
    for (;;) {
        auto job = w_.flips->take();
        if (!job) break;
        if (job->epoch != epoch_) {
            ++stale_drops_;
            continue;
        }
        raise_job_(std::move(job));
    }
}

void HdOsdScreen::tick(const PageDescription* page) noexcept {
    TASTY_SEAT_BODY(HdOsdScreen);
    retry_drop_();
    if (w_.status != nullptr) (void)status_seen_.take_if_changed(*w_.status, status_);
    const bool live =
        status_.state == HdOsdStatus::State::Ready || status_.state == HdOsdStatus::State::Showing;
    if (page == nullptr || !live) {
        close_();
        return;
    }
    page_ = *page;
    if (open_ && w_.fb != nullptr && w_.fb->held()) {
        close_();
        return;
    }
    if (open_) on_applies_();
    if (!open_) {
        if (available()) (void)try_open_();
    } else if (page_ != page_sent_) {
        publish_ask_(true);
    }
    if (open_) take_jobs_();
    if (reraise_owed_) reraise_owed_ = !reraise_();
}

bool HdOsdScreen::covers_hdmi() const noexcept {
    TASTY_SEAT_BODY(HdOsdScreen);
    return covers_;
}

bool HdOsdScreen::available() const noexcept {
    TASTY_SEAT_BODY(HdOsdScreen);
    if (status_.state != HdOsdStatus::State::Ready && status_.state != HdOsdStatus::State::Showing)
        return false;
    if (w_.fb == nullptr || w_.fb->held()) return false;
    return !w_.fb->raised() || ours_;
}

std::uint32_t HdOsdScreen::stale_drops() const noexcept {
    TASTY_SEAT_BODY(HdOsdScreen);
    return stale_drops_;
}

}  // namespace mister::app
