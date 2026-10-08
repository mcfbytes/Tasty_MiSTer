// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/backdrop_sampler.h"

#include <array>
#include <cstdint>
#include <optional>

namespace mister::app {

BackdropSampler::BackdropSampler(hal::ScalerBuffers window, const os::IClock* clock) noexcept
    : window_(std::move(window)), clock_(clock) {}

bool BackdropSampler::note_applies(std::uint32_t applies, std::int64_t now) noexcept {
    TASTY_SEAT_BODY(BackdropSampler);
    if (have_applies_ && applies == applies_) return false;
    have_applies_ = true;
    applies_ = applies;
    restart(now);
    return true;
}

void BackdropSampler::restart(std::int64_t now) noexcept {
    TASTY_SEAT_BODY(BackdropSampler);
    probe_.restart(now);
    phase_ = Phase::Probe;
    next_poll_ = 0;
    lowlat_ = false;
    stuck_ = false;
    live_ = false;
    torn_ = 0;
    width_ = height_ = line_ = 0;
    view_n_ = 0;
    stride_ = 0;
    stride_mib_ = 0;
}

bool BackdropSampler::poll_due(std::int64_t now) const noexcept {
    return phase_ == Phase::Probe && (next_poll_ == 0 || now >= next_poll_);
}

int BackdropSampler::park_ms(std::int64_t now) const noexcept {
    if (phase_ != Phase::Probe) return 1000;
    if (next_poll_ == 0 || now >= next_poll_) return 0;
    const std::int64_t left = next_poll_ - now;
    const std::int64_t ms = (left + 999'999) / 1'000'000;
    return ms > 4 ? 4 : static_cast<int>(ms);
}

BackdropSampler::Image BackdropSampler::poll(std::int64_t now, bool copy) noexcept {
    TASTY_SEAT_BODY(BackdropSampler);
    if (phase_ == Phase::Probe && (next_poll_ == 0 || now >= next_poll_)) {
        probe_.read(window_);
        next_poll_ = now + kPollNs;
        const auto verdict = probe_.decide(now);
        if (verdict == hal::ScalerProbe::Verdict::Dead) {
            phase_ = Phase::Idle;
            stuck_ = hal::ScalerProbe::port_stuck(window_);
        } else if (verdict != hal::ScalerProbe::Verdict::Pending) {
            phase_ = Phase::Run;
            lowlat_ = verdict == hal::ScalerProbe::Verdict::LargeLowlat;
            stride_ = verdict == hal::ScalerProbe::Verdict::Small
                          ? hal::ScalerBuffers::kStrideSmall
                          : hal::ScalerBuffers::kStrideLarge;
            stride_mib_ = static_cast<std::uint8_t>(stride_ >> 20);
        }
    }
    const bool fresh = copy && phase_ == Phase::Run && copy_frame_();
    return image_(fresh);
}

BackdropSampler::Image BackdropSampler::image_(bool fresh) const noexcept {
    Image img;
    img.rgb = std::span<const std::byte>{cache_.data(), view_n_};
    img.width = width_;
    img.height = height_;
    img.line = line_;
    img.live = live_;
    img.fresh = fresh && live_;
    img.lowlat = lowlat_;
    img.stuck = stuck_;
    img.stride_mib = stride_mib_;
    img.torn = torn_;
    return img;
}

bool BackdropSampler::copy_frame_() noexcept {
    const auto load = [this](std::size_t buf) -> std::optional<hal::ScalerHeader> {
        const auto h = window_.header(buf * stride_);
        if (!h || !h->supported() || h->width == 0 || h->height == 0) return std::nullopt;
        if (static_cast<unsigned>(h->width) * 3u > h->line) return std::nullopt;
        return *h;
    };
    std::optional<hal::ScalerHeader> header;
    std::size_t buf = 0;
    if (lowlat_) {
        header = load(0);
    } else {
        std::array<hal::ScalerHeader, hal::ScalerBuffers::kBuffers> hs{};
        auto live = probe_.moved(stride_);
        for (std::size_t i = 0; i < hs.size(); ++i) {
            if (!live[i]) continue;
            if (const auto h = load(i))
                hs[i] = *h;
            else
                live[i] = false;
        }
        const auto pick = hal::ScalerProbe::pick_complete(hs, live);
        if (!pick) return false;
        buf = pick->buf;
        header = hs[buf];
    }
    if (!header) return false;
    const std::size_t nbytes =
        static_cast<std::size_t>(header->line) * static_cast<std::size_t>(header->height);
    if (nbytes == 0 || header->header_size > stride_ || nbytes > stride_ - header->header_size)
        return false;
    if (cache_.size() < nbytes) cache_.resize(nbytes);
    const std::uint8_t before = header->frame_counter();
    if (clock_ != nullptr) (void)clock_->now();
    if (!window_.copy(buf * stride_ + header->header_size,
                      std::span<std::byte>{cache_.data(), nbytes}))
        return false;
    if (clock_ != nullptr) (void)clock_->now();
    width_ = header->width;
    height_ = header->height;
    line_ = header->line;
    view_n_ = nbytes;
    live_ = true;
    if (lowlat_) {
        const auto after = window_.header(buf * stride_);
        if (after && after->frame_counter() != before) ++torn_;
    }
    return true;
}

}  // namespace mister::app
