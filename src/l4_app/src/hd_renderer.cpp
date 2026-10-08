// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hd_renderer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "app/hd_layout.h"
#include "app/hd_style.h"
#include "app/hd_theme.h"

namespace mister::app {
namespace {

HdRect box_of(HdLayout::Rect r) noexcept { return HdRect{r.x, r.y, r.w, r.h}; }

[[nodiscard]] std::uint16_t pack565(std::uint32_t rgb) noexcept {
    const auto r = static_cast<std::uint8_t>(rgb >> 16);
    const auto g = static_cast<std::uint8_t>(rgb >> 8);
    const auto b = static_cast<std::uint8_t>(rgb);
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(r >> 3) << 11) |
                                      (static_cast<std::uint16_t>(g >> 2) << 5) |
                                      static_cast<std::uint16_t>(b >> 3));
}

[[nodiscard]] HdRect intersect(HdRect a, HdRect b) noexcept {
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0) return {};
    const std::int32_t x0 = a.x > b.x ? a.x : b.x;
    const std::int32_t y0 = a.y > b.y ? a.y : b.y;
    const std::int32_t x1 = (a.x + a.w) < (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    const std::int32_t y1 = (a.y + a.h) < (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    if (x1 <= x0 || y1 <= y0) return {};
    return HdRect{x0, y0, x1 - x0, y1 - y0};
}

[[nodiscard]] std::uint32_t us_of(std::uint64_t t0, std::uint64_t t1) noexcept {
    const std::uint64_t us = (t1 >= t0 ? t1 - t0 : 0) / 1000u;
    return us > 0xffffffffu ? 0xffffffffu : static_cast<std::uint32_t>(us);
}

HdRect clip_box(HdRect r, std::int32_t w, std::int32_t h) noexcept {
    if (r.w <= 0 || r.h <= 0) return {};
    if (r.x < 0) {
        r.w += r.x;
        r.x = 0;
    }
    if (r.y < 0) {
        r.h += r.y;
        r.y = 0;
    }
    if (r.w <= 0 || r.h <= 0 || r.x >= w || r.y >= h) return {};
    if (r.x + r.w > w) r.w = w - r.x;
    if (r.y + r.h > h) r.h = h - r.y;
    return r;
}

}  // namespace

void HdRenderer::bind_frame(HdFlipChannel* flips, hal::FpgaMemory mem,
                            std::unique_ptr<HdRaster> raster) noexcept {
    flips_ = flips;
    slots_ = std::move(mem);
    raster_ = std::move(raster);
}

void HdRenderer::bind_scaler(hal::ScalerBuffers window) noexcept {
    sampler_.emplace(std::move(window), w_.clock);
}

void HdRenderer::set_fps(std::uint8_t fps) noexcept {
    pacer_.set_fps(fps);
    st_.fps = pacer_.fps();
}

void HdRenderer::begin() noexcept {
    TASTY_SEAT_BODY(HdRenderer);
    up_ = true;
    st_ = {};
    st_.fps = pacer_.fps();
    if (raster_ || sampler_) {
        const HdTheme theme = derive_theme(kHdPresets[0], pacer_.fps());
        scaler_.open(theme);
        if (raster_) {
            if (!raster_->open(theme, layout_for(0, 0))) {
                st_.state = HdOsdStatus::State::Failed;
                st_.fail = HdOsdStatus::Fail::RasterInit;
                publish_();
                return;
            }
            raster_open_ = true;
        }
    }
    st_.state = HdOsdStatus::State::Ready;
    st_.fail = HdOsdStatus::Fail::None;
    publish_();
}

void HdRenderer::serve() noexcept {
    TASTY_SEAT_BODY(HdRenderer);
    if (!up_ || st_.state == HdOsdStatus::State::Failed) return;

    const bool pixels = flips_ != nullptr && slots_ && raster_open_;
    const std::uint64_t now = now_ns_();
    if (pixels) reap_(true, now);

    bool sampled = false;
    if (w_.ask != nullptr) {
        HdOsdAsk got{};
        if (reader_.take_if_changed(*w_.ask, got)) {
            const bool opened = got.open && !ask_.open;
            ask_ = got;
            if (opened) {
                pacer_.resync();
                if (sampler_) {
                    sampler_->restart(static_cast<std::int64_t>(now));
                    badge_.reset();
                }
            }
            sampled = true;
        }
    }

    if (!ask_.open) {
        st_.state = HdOsdStatus::State::Ready;
        st_.fail = HdOsdStatus::Fail::None;
        st_.fps = pacer_.fps();
        if (sampled) publish_();
        return;
    }
    if (pixels && sampler_) {
        if (sampler_->note_applies(ask_.video_applies, static_cast<std::int64_t>(now)))
            badge_.reset();
        if (sampler_->poll_due(static_cast<std::int64_t>(now))) {
            const auto img = sampler_->poll(static_cast<std::int64_t>(now), false);
            if (img.stuck) {
                st_.state = HdOsdStatus::State::Failed;
                st_.fail = HdOsdStatus::Fail::PortStuck;
                st_.fps = pacer_.fps();
                publish_();
                return;
            }
        }
    }
    if (!pacer_.due(now)) {
        if (sampled) publish_();
        return;
    }
    pacer_.advance(now);
    ++ticks_;
    st_.fps = pacer_.fps();
    if (!pixels) {
        st_.state = HdOsdStatus::State::Ready;
        st_.fail = HdOsdStatus::Fail::None;
        publish_();
        return;
    }
    paint_(now);
}

bool HdRenderer::idle() const noexcept {
    if (w_.ask != nullptr) {
        const std::uint32_t g = w_.ask->generation();
        if (g != 0 && g != reader_.seen()) return false;
    }
    if (!up_ || !ask_.open || st_.state == HdOsdStatus::State::Failed) return true;
    const std::uint64_t now = now_ns_();
    if (sampler_ && sampler_->poll_due(static_cast<std::int64_t>(now))) return false;
    return !pacer_.due(now);
}

int HdRenderer::park_ms() const noexcept {
    if (!up_ || !ask_.open || w_.clock == nullptr || st_.state == HdOsdStatus::State::Failed)
        return 1000;
    const std::uint64_t now = now_ns_();
    int ms = pacer_.park_ms(now);
    if (sampler_ && sampler_->probing()) {
        const int probe = sampler_->park_ms(static_cast<std::int64_t>(now));
        if (probe < ms) ms = probe;
    }
    return ms;
}

void HdRenderer::release() noexcept {
    TASTY_SEAT_BODY(HdRenderer);
    reap_(false, 0);
    if (raster_ && raster_open_) {
        raster_->close();
        raster_open_ = false;
    }
    up_ = false;
    ask_.open = false;
    st_.state = HdOsdStatus::State::Off;
    publish_();
}

void HdRenderer::publish_() noexcept {
    if (w_.status != nullptr) w_.status->publish(st_);
}

std::uint64_t HdRenderer::now_ns_() const noexcept {
    if (w_.clock == nullptr) return 0;
    const auto n = w_.clock->now().count();
    return n < 0 ? 0 : static_cast<std::uint64_t>(n);
}

void HdRenderer::drop_held_() noexcept {
    for (auto& held : held_)
        held = {};
}

void HdRenderer::reap_(bool hold, std::uint64_t now) noexcept {
    if (!hold) drop_held_();
    if (flips_ == nullptr) return;
    for (;;) {
        HdFlipChannel::Loan loan = flips_->reap();
        if (!loan) return;
        if (!hold) continue;
        bool parked = false;
        for (std::size_t i = 0; i < held_.size(); ++i) {
            if (held_[i]) continue;
            held_[i] = std::move(loan);
            held_at_[i] = now;
            parked = true;
            break;
        }
        if (!parked) loan = {};
    }
}

HdFlipChannel::Loan HdRenderer::writable_(std::uint64_t now) noexcept {
    if (HdFlipChannel::Loan fresh = flips_->acquire()) return fresh;
    for (std::size_t i = 0; i < held_.size(); ++i) {
        if (!held_[i] || now - held_at_[i] < kFlipLatchNs) continue;
        held_at_[i] = 0;
        return std::move(held_[i]);
    }
    return {};
}

bool HdRenderer::ensure_frame_() noexcept {
    const HdSurface& s = ask_.surface;
    const std::size_t bytes = static_cast<std::size_t>(s.stride) * s.h;
    const bool stride_ok = s.stride >= s.w * 2u && (s.stride % 2u) == 0u;
    const bool fits = slots_ && bytes <= kMaxFrameBytes &&
                      static_cast<std::size_t>(s.slot[0]) + bytes <= slots_->region().len &&
                      static_cast<std::size_t>(s.slot[1]) + bytes <= slots_->region().len;
    if (s.w == 0 || s.h == 0 || !stride_ok || !fits) {
        st_.state = HdOsdStatus::State::Failed;
        st_.fail = HdOsdStatus::Fail::NoSlotWindow;
        publish_();
        return false;
    }
    const std::size_t px = bytes / 2u;
    if (frame_.size() != px) frame_.assign(px, 0);
    return true;
}

void HdRenderer::paint_(std::uint64_t now) noexcept {
    if (!ensure_frame_()) return;
    const HdSurface& s = ask_.surface;
    const HdLayout layout = layout_for(s.w, s.h);
    bool reset_frame = false;
    if (ask_.epoch != epoch_seen_) {
        epoch_seen_ = ask_.epoch;
        ledger_.reset(box_of(layout.panel), box_of(layout.game));
        const std::uint16_t fill = pack565(derive_theme(kHdPresets[0], pacer_.fps()).primary);
        std::fill(frame_.begin(), frame_.end(), fill);
        seq_ = 0;
        reset_frame = true;
        if (raster_) raster_->relayout(layout);
    }
    ledger_.begin_tick();
    const std::uint64_t t0 = now_ns_();
    if (!backdrop_(layout, reset_frame)) return;
    const std::uint64_t ts1 = now_ns_();
    st_.sample_us_last = us_of(t0, ts1);
    if (st_.sample_us_last > st_.sample_us_max) st_.sample_us_max = st_.sample_us_last;
    HdFlipChannel::Loan loan = writable_(now);
    if (!loan) {
        ledger_.invalidate(box_of(layout.panel), box_of(layout.game));
        ++st_.skipped;
        if (st_.state != HdOsdStatus::State::Showing) st_.state = HdOsdStatus::State::Ready;
        st_.fail = HdOsdStatus::Fail::None;
        publish_();
        return;
    }

    const std::uint8_t idx = flips_->index_of(loan);
    const std::uint32_t stride_px = s.stride / 2u;
    const std::size_t origin_i = static_cast<std::size_t>(layout.panel.y) * stride_px +
                                 static_cast<std::size_t>(layout.panel.x);
    if (layout.panel.x < 0 || layout.panel.y < 0 || origin_i > frame_.size()) {
        st_.state = HdOsdStatus::State::Failed;
        st_.fail = HdOsdStatus::Fail::NoSlotWindow;
        publish_();
        return;
    }

    const std::uint64_t tr0 = now_ns_();
    const HdBadgeView shown{st_.badge, pacer_.fps()};
    const HdRect dirty = raster_->render(
        ask_.page, shown,
        std::span<std::uint16_t>{frame_.data() + origin_i, frame_.size() - origin_i}, stride_px);
    const std::uint64_t tr1 = now_ns_();
    st_.render_us_last = us_of(tr0, tr1);
    if (st_.render_us_last > st_.render_us_max) st_.render_us_max = st_.render_us_last;

    HdRect panel_dirty = dirty;
    panel_dirty.x += layout.panel.x;
    panel_dirty.y += layout.panel.y;
    const HdRect clipped = clip_box(intersect(panel_dirty, box_of(layout.panel)),
                                    static_cast<std::int32_t>(s.w), static_cast<std::int32_t>(s.h));
    if (clipped.w > 0 && clipped.h > 0) ledger_.mark(HdDirtyLedger::Region::Panel, clipped);

    const std::uint64_t tc0 = now_ns_();
    copy_rect_(idx, ledger_.consume(idx, HdDirtyLedger::Region::Game));
    copy_rect_(idx, ledger_.consume(idx, HdDirtyLedger::Region::Panel));
    slots_->handoff();
    const std::uint64_t tc1 = now_ns_();
    st_.copy_us_last = us_of(tc0, tc1);
    if (st_.copy_us_last > st_.copy_us_max) st_.copy_us_max = st_.copy_us_last;

    loan->epoch = ask_.epoch;
    loan->seq = ++seq_;
    loan->view = FbView{.offset = s.slot[idx],
                        .width = s.w,
                        .height = s.h,
                        .stride = s.stride,
                        .format = FbView::FbFormat::Rgb565};
    flips_->send(std::move(loan));

    const std::uint64_t t1 = now_ns_();
    if (t1 >= t0 && t1 - t0 >= kDeadlineNs) {
        st_.state = HdOsdStatus::State::Failed;
        st_.fail = HdOsdStatus::Fail::Deadline;
    } else {
        st_.state = HdOsdStatus::State::Showing;
        st_.fail = HdOsdStatus::Fail::None;
    }
    publish_();
}

bool HdRenderer::backdrop_(const HdLayout& layout, bool reset_frame) noexcept {
    if (!sampler_) {
        st_.badge = HdOsdStatus::Badge::Unknown;
        return true;
    }
    const auto img = sampler_->poll(static_cast<std::int64_t>(now_ns_()), true);
    if (img.stuck) {
        st_.state = HdOsdStatus::State::Failed;
        st_.fail = HdOsdStatus::Fail::PortStuck;
        st_.fps = pacer_.fps();
        publish_();
        return false;
    }
    st_.backdrop_w = img.live ? img.width : 0;
    st_.backdrop_h = img.live ? img.height : 0;
    st_.stride_mib = img.stride_mib;
    st_.lowlat = img.lowlat ? 1 : 0;
    if (!img.live) {
        st_.badge = HdOsdStatus::Badge::Unknown;
        return true;
    }
    const std::uint32_t stride_px = ask_.surface.stride / 2u;
    const HdRect dirty =
        scaler_.scale(img.rgb, img.width, img.height, img.line, frame_, stride_px,
                      box_of(layout.game), box_of(layout.cover), img.fresh || reset_frame);
    if (dirty.w > 0 && dirty.h > 0) ledger_.mark(HdDirtyLedger::Region::Game, dirty);
    if (img.fresh) st_.badge = badge_.observe(img.rgb, img.width, img.height, img.line);
    return true;
}

void HdRenderer::copy_rect_(std::uint8_t slot, HdRect rect) noexcept {
    const HdSurface& s = ask_.surface;
    if (rect.w <= 0 || rect.h <= 0 || rect.x < 0 || rect.y < 0) return;
    if (static_cast<std::uint32_t>(rect.x) + static_cast<std::uint32_t>(rect.w) > s.w) return;
    if (static_cast<std::uint32_t>(rect.y) + static_cast<std::uint32_t>(rect.h) > s.h) return;
    const std::uint32_t stride_px = s.stride / 2u;
    const std::size_t bytes = static_cast<std::size_t>(s.stride) * s.h;
    std::span<std::byte> dst = slots_->view(s.slot[slot & 1u], bytes);
    for (std::int32_t row = 0; row < rect.h; ++row) {
        const std::size_t y = static_cast<std::size_t>(rect.y + row);
        const std::size_t x = static_cast<std::size_t>(rect.x);
        const auto* src = reinterpret_cast<const std::byte*>(frame_.data() + y * stride_px + x);
        hal::copy_to_uncached(dst.data() + y * s.stride + x * 2u, src,
                              static_cast<std::size_t>(rect.w) * 2u);
    }
}

}  // namespace mister::app
