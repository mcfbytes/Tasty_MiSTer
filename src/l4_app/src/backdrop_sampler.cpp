// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/backdrop_sampler.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>

#include "app/rgb_rows.h"

namespace mister::app {

namespace {

[[nodiscard]] os::RawMonotonicClock::time_point raw_instant(
    std::chrono::nanoseconds since_epoch
) noexcept {
    return os::RawMonotonicClock::time_point{since_epoch};
}

}

BackdropSampler::BackdropSampler(hal::ScalerBuffers window) noexcept : window_(std::move(window)) {}

bool BackdropSampler::note_applies(std::uint32_t applies, std::int64_t now) noexcept {
    TASTY_SEAT_BODY(BackdropSampler);
    if (have_applies_ && applies == applies_)
        return false;
    have_applies_ = true;
    applies_ = applies;
    known_stride_.reset();
    restart(now);
    return true;
}

void BackdropSampler::restart(std::int64_t now) noexcept {
    TASTY_SEAT_BODY(BackdropSampler);
    probe_.restart(raw_instant(std::chrono::nanoseconds{now}));
    phase_ = Phase::Probe;
    next_poll_ = 0;
    lowlat_ = false;
    stuck_ = false;
    still_ = false;
    live_ = false;
    have_prev_ = false;
    same_ = false;
    torn_ = 0;
    width_ = height_ = line_ = 0;
    view_n_ = 0;
    stride_ = hal::ScalerStride::Large;
}

bool BackdropSampler::poll_due(std::int64_t now) const noexcept {
    return phase_ == Phase::Probe && (next_poll_ == 0 || now >= next_poll_);
}

infra::ParkTimeout BackdropSampler::park_ms(std::int64_t now) const noexcept {
    constexpr std::chrono::milliseconds kProbeCeil{4};
    constexpr infra::ParkTimeout kIdlePoll{1000};
    if (phase_ != Phase::Probe)
        return kIdlePoll;
    if (next_poll_ == 0 || now >= next_poll_)
        return infra::kNoWait;
    const std::int64_t left = next_poll_ - now;
    const auto ms = std::chrono::ceil<std::chrono::milliseconds>(std::chrono::nanoseconds{left});
    return std::chrono::duration_cast<infra::ParkTimeout>(std::min(ms, kProbeCeil));
}

BackdropSampler::Image BackdropSampler::poll(std::int64_t now, bool copy) noexcept {
    TASTY_SEAT_BODY(BackdropSampler);
    same_ = false;
    if (phase_ == Phase::Probe && (next_poll_ == 0 || now >= next_poll_)) {
        probe_.read(window_);
        next_poll_ = now + kPollNs;
        const auto verdict = probe_.decide(raw_instant(std::chrono::nanoseconds{now}));
        if (verdict == hal::ScalerProbe::Verdict::Dead) {
            if (const auto held = hal::ScalerProbe::still(window_, known_stride_)) {
                run_(*held, true);
            } else {
                phase_ = Phase::Idle;
                stuck_ = hal::ScalerProbe::port_stuck(window_);
            }
        } else if (verdict != hal::ScalerProbe::Verdict::Pending) {
            run_(verdict, false);
        }
    }
    if (!copy || phase_ != Phase::Run)
        return image_(false);
    if (still_) {

        if (marks_() != still_marks_) {
            restart(now);
            return image_(false);
        }
        if (have_prev_) {
            same_ = true;
            return image_(false);
        }
    }
    return image_(copy_frame_());
}

void BackdropSampler::run_(hal::ScalerProbe::Verdict verdict, bool still) noexcept {
    const auto layout = hal::ScalerProbe::layout_of(verdict);
    if (!layout)
        return;
    phase_ = Phase::Run;
    lowlat_ = layout->lowlat;
    stride_ = layout->stride;
    still_ = still;
    if (still)
        still_marks_ = marks_();
    else
        known_stride_ = stride_;
}

BackdropSampler::Marks BackdropSampler::marks_() const noexcept {
    Marks out{};
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto h = window_.header(i * hal::stride_bytes(stride_));
        if (h && h->supported())
            out[i] = *h;
    }
    return out;
}

BackdropSampler::Image BackdropSampler::image_(bool fresh) const noexcept {
    Image img;
    img.rgb = RgbRows{
        .bytes = std::span<const std::byte>{cache_.data(), view_n_},
        .stride = RowBytes{line_},
        .rows = PictureRows{height_}
    };
    img.width = width_;
    img.live = live_;
    img.fresh = fresh && live_;
    img.same = same_ && live_;
    img.lowlat = lowlat_;
    img.stuck = stuck_;

    if (phase_ == Phase::Run)
        img.stride_mib = hal::stride_mib(stride_);
    img.torn = torn_;
    return img;
}

bool BackdropSampler::copy_frame_() noexcept {
    const std::size_t spacing = hal::stride_bytes(stride_);
    const auto load = [this, spacing](std::size_t buf) -> std::optional<hal::ScalerHeader> {
        const auto h = window_.header(buf * spacing);
        if (!h || !h->supported() || h->width == 0 || h->height == 0)
            return std::nullopt;
        if (RowBytes{h->line}.lacks(rgb_row_of(h->width)))
            return std::nullopt;
        return *h;
    };
    std::optional<hal::ScalerHeader> header;
    std::size_t buf = 0;
    if (lowlat_) {
        header = load(0);
    } else {
        std::array<hal::ScalerHeader, hal::ScalerBuffers::kBuffers> hs{};
        auto live = still_ ? hal::ScalerProbe::kAllLive : probe_.moved(stride_);
        for (std::size_t i = 0; i < hs.size(); ++i) {
            if (!live[i])
                continue;
            if (const auto h = load(i))
                hs[i] = *h;
            else
                live[i] = false;
        }
        const auto pick = hal::ScalerProbe::pick_complete(hs, live);
        if (!pick)
            return false;
        buf = pick->buf;
        header = hs[buf];
    }
    if (!header)
        return false;
    const std::size_t nbytes = std::size_t{header->line} * header->height;
    if (nbytes == 0 || header->header_size > spacing || nbytes > spacing - header->header_size)
        return false;
    if (cache_.size() < nbytes)
        cache_.resize(nbytes);
    if (prev_.size() < nbytes)
        prev_.resize(nbytes);
    const auto before = header->frame_counter();
    if (!window_
             .copy(buf * spacing + header->header_size, std::span<std::byte>{prev_.data(), nbytes}))
        return false;
    same_ = have_prev_
        && nbytes == view_n_
        && header->width == width_
        && header->height == height_
        && header->line == line_
        && std::memcmp(prev_.data(), cache_.data(), nbytes) == 0;
    cache_.swap(prev_);
    width_ = header->width;
    height_ = header->height;
    line_ = header->line;
    view_n_ = nbytes;
    live_ = true;
    have_prev_ = true;
    if (lowlat_) {
        const auto after = window_.header(buf * spacing);
        if (after && after->frame_counter() != before)
            ++torn_;
    }
    return true;
}

}
