// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hd_renderer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

#include "app/hd_layout.h"
#include "app/hd_style.h"
#include "app/hd_theme.h"
#include "infra/byte_view.h"

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

HdRenderer::HdRenderer(const Wiring& w, std::optional<Frame> frame, std::uint8_t fps) noexcept
    : w_(w), pacer_(fps) {
    if (!frame) return;
    flips_ = infra::opt_ref_of(&frame->flips);
    slots_ = std::move(frame->mem);
    raster_ = std::move(frame->raster);
}

void HdRenderer::bind_scaler(hal::ScalerBuffers window) noexcept {
    scaler_uio_ = window.uio_bound();
    sampler_.emplace(std::move(window), &w_.clock);
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

    st_.uio = static_cast<std::uint8_t>((slots_ && slots_->uio_bound() ? 1u : 0u) |
                                        (scaler_uio_ ? 2u : 0u));
    if (raster_ || sampler_) {
        const HdTheme theme = derive_theme(kHdPresets[0]);
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

    const bool pixels = flips_ && slots_ && raster_open_;
    const std::uint64_t now = now_ns_();
    if (pixels) reap_(true, now);

    bool sampled = false;
    HdOsdAsk got{};
    if (reader_.take_if_changed(w_.ask, got)) {
        const bool opened = got.open && !ask_.open;
        const bool closed = !got.open && ask_.open;
        ask_ = got;
        if (st_.badge != got.badge) panel_owed_ = true;
        st_.badge = got.badge;
        if (opened) {
            pacer_.resync();
            if (sampler_) sampler_->restart(static_cast<std::int64_t>(now));
        }
        if (closed) close_job_();
        fresh_ask_ = got.open;
        sampled = true;
    }
    const bool rolled = vis_.roll(now, st_);

    if (!ask_.open) {
        st_.state = HdOsdStatus::State::Ready;
        st_.fail = HdOsdStatus::Fail::None;
        st_.fps = pacer_.fps();
        if (sampled || rolled) publish_();
        return;
    }
    if (pixels && sampler_) {
        (void)sampler_->note_applies(ask_.video_applies, static_cast<std::int64_t>(now));
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
    if (!pixels) {
        if (!pacer_.due(now)) {
            if (sampled || rolled) publish_();
            return;
        }
        pacer_.advance(now);
        ++ticks_;
        st_.fps = pacer_.fps();
        st_.state = HdOsdStatus::State::Ready;
        st_.fail = HdOsdStatus::Fail::None;
        publish_();
        return;
    }
    draw_(now);
}

bool HdRenderer::idle() const noexcept {
    const std::uint32_t g = w_.ask.generation();
    if (g != 0 && g != reader_.seen()) return false;
    if (!up_ || !ask_.open || st_.state == HdOsdStatus::State::Failed) return true;
    const std::uint64_t now = now_ns_();
    if (sampler_ && sampler_->poll_due(static_cast<std::int64_t>(now))) return false;
    const bool pixels = flips_ && slots_ && raster_open_;
    if (!pixels || !epoch_valid_) return !pacer_.due(now);
    if (panel_owed_) return false;
    return !can_progress_(now);
}

int HdRenderer::park_ms() const noexcept {
    if (!up_ || !ask_.open || st_.state == HdOsdStatus::State::Failed) return 1000;
    const std::uint64_t now = now_ns_();
    const bool pixels = flips_ && slots_ && raster_open_;
    int ms = pixels && job_.busy() ? 1000 : pacer_.park_ms(now);
    if (pixels && epoch_valid_) {
        const std::uint64_t wait = wait_ns_(now);
        if (wait != std::numeric_limits<std::uint64_t>::max()) {
            const std::uint64_t wait_ms = (wait + 999'999ull) / 1'000'000ull;
            if (wait_ms < static_cast<std::uint64_t>(ms)) ms = static_cast<int>(wait_ms);
        }
    }
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
    close_job_();
    up_ = false;
    ask_.open = false;
    st_.state = HdOsdStatus::State::Off;
    publish_();
}

void HdRenderer::publish_() noexcept { w_.status.publish(st_); }

std::uint64_t HdRenderer::now_ns_() const noexcept {
    const auto n = w_.clock.now().count();
    return n < 0 ? 0 : static_cast<std::uint64_t>(n);
}

bool HdRenderer::ask_moved_() const noexcept {
    const std::uint32_t g = w_.ask.generation();
    return g != 0 && g != reader_.seen();
}

void HdRenderer::reap_(bool hold, std::uint64_t now) noexcept {
    if (!hold) {
        for (auto& loan : own_)
            loan = {};
        last_sent_ = kNoSlot;
    }
    if (!flips_) return;
    for (;;) {
        HdFlipChannel::Loan loan = flips_->reap();
        if (!loan) break;
        if (!hold) continue;
        const std::uint8_t i = flips_->index_of(loan);
        if (i >= own_.size()) continue;
        own_[i] = std::move(loan);
        free_at_[i] = now + kFlipLatchNs;
    }
    if (!hold) return;
    for (;;) {
        HdFlipChannel::Loan loan = flips_->acquire();
        if (!loan) break;
        const std::uint8_t i = flips_->index_of(loan);
        if (i >= own_.size()) continue;
        own_[i] = std::move(loan);
        free_at_[i] = 0;
    }
}

bool HdRenderer::owned_(std::uint8_t slot, std::uint64_t now) const noexcept {
    return slot < own_.size() && own_[slot] && now >= free_at_[slot];
}

bool HdRenderer::stale_(std::uint8_t slot) const noexcept {
    const HdRect g = ledger_.peek(slot, HdDirtyLedger::Region::Game);
    const HdRect p = ledger_.peek(slot, HdDirtyLedger::Region::Panel);
    return (g.w > 0 && g.h > 0) || (p.w > 0 && p.h > 0);
}

bool HdRenderer::flip_wanted_() const noexcept {
    return last_sent_ == kNoSlot || stale_(last_sent_);
}

std::uint8_t HdRenderer::flip_target_(std::uint64_t now) const noexcept {
    if (last_sent_ != kNoSlot) return static_cast<std::uint8_t>(last_sent_ ^ 1u);
    for (std::uint8_t i = 0; i < own_.size(); ++i)
        if (owned_(i, now) && !stale_(i)) return i;
    if (job_.step() == BackdropJob::Step::Copy) return job_.target();
    for (std::uint8_t i = 0; i < own_.size(); ++i)
        if (owned_(i, now)) return i;
    return kNoSlot;
}

bool HdRenderer::can_progress_(std::uint64_t now) const noexcept {
    switch (job_.step()) {
        case BackdropJob::Step::Scale:
            return true;
        case BackdropJob::Step::Copy:
            return owned_(job_.target(), now);
        case BackdropJob::Step::Idle:
            break;
    }
    if (pacer_.due(now)) return true;
    if (!flip_wanted_()) return false;
    const std::uint8_t o = flip_target_(now);
    if (o == kNoSlot || !owned_(o, now)) return false;
    const HdRect g = ledger_.peek(o, HdDirtyLedger::Region::Game);
    return g.w <= 0 || g.h <= 0 || job_.committed();
}

std::uint64_t HdRenderer::wait_ns_(std::uint64_t now) const noexcept {
    constexpr std::uint64_t kLentPollNs = 5'000'000;
    std::uint64_t best = std::numeric_limits<std::uint64_t>::max();
    const auto consider = [&](std::uint8_t i) noexcept {
        if (i >= own_.size()) return;
        if (!own_[i]) {
            if (kLentPollNs < best) best = kLentPollNs;
        } else if (now < free_at_[i] && free_at_[i] - now < best) {
            best = free_at_[i] - now;
        }
    };
    if (job_.step() == BackdropJob::Step::Copy) consider(job_.target());
    if (job_.step() == BackdropJob::Step::Idle && flip_wanted_()) {
        if (last_sent_ != kNoSlot) {
            consider(static_cast<std::uint8_t>(last_sent_ ^ 1u));
        } else {
            consider(0);
            consider(1);
        }
    }
    return best;
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

void HdRenderer::close_job_() noexcept {
    job_.abort();
    epoch_valid_ = false;
    pending_ = false;
    panel_owed_ = false;
    fresh_ask_ = false;
}

void HdRenderer::start_epoch_(const HdLayout& layout) noexcept {
    epoch_seen_ = ask_.epoch;
    epoch_valid_ = true;
    layout_ = layout;
    ledger_.reset(box_of(layout.panel), box_of(layout.game));
    const std::uint16_t fill = pack565(derive_theme(kHdPresets[0]).primary);
    std::fill(frame_.begin(), frame_.end(), fill);
    job_.reset_canvas();
    seq_ = 0;
    last_sent_ = kNoSlot;
    fill_gen_ = ++canvas_gen_;
    slot_gen_ = {};
    shown_gen_ = 0;
    bd_reset_ = true;
    fresh_ask_ = true;
    pacer_.resync();
    if (raster_) raster_->relayout(layout);
}

void HdRenderer::draw_(std::uint64_t now) noexcept {
    if (!ensure_frame_()) return;
    const HdSurface& s = ask_.surface;
    const HdLayout layout = layout_for(s.w, s.h);
    const std::uint32_t stride_px = s.stride / 2u;
    const std::size_t origin_i = static_cast<std::size_t>(layout.panel.y) * stride_px +
                                 static_cast<std::size_t>(layout.panel.x);
    if (layout.panel.x < 0 || layout.panel.y < 0 || origin_i > frame_.size()) {
        st_.state = HdOsdStatus::State::Failed;
        st_.fail = HdOsdStatus::Fail::NoSlotWindow;
        publish_();
        return;
    }
    if (!epoch_valid_ || ask_.epoch != epoch_seen_) start_epoch_(layout);
    deadline_ = false;
    st_.fps = pacer_.fps();

    const bool asked = fresh_ask_;
    fresh_ask_ = false;
    if (asked || panel_owed_) {
        panel_step_(layout, asked);
    } else {
        place_(now);
    }

    const std::uint64_t t0 = now_ns_();
    for (;;) {
        const bool sliced = slice_(now_ns_());
        if (st_.state == HdOsdStatus::State::Failed) return;
        const bool flipped = flip_(now_ns_());
        if (!sliced && !flipped) break;
        if (panel_owed_ || ask_moved_() || now_ns_() - t0 >= kSliceNs) break;
    }
    if (st_.state == HdOsdStatus::State::Failed) return;
    if (deadline_) {
        st_.state = HdOsdStatus::State::Failed;
        st_.fail = HdOsdStatus::Fail::Deadline;
    } else {
        if (st_.state != HdOsdStatus::State::Showing) st_.state = HdOsdStatus::State::Ready;
        st_.fail = HdOsdStatus::Fail::None;
    }
    publish_();
}

void HdRenderer::panel_step_(const HdLayout& layout, bool asked) noexcept {
    const HdSurface& s = ask_.surface;
    const std::uint32_t stride_px = s.stride / 2u;
    const std::size_t origin_i = static_cast<std::size_t>(layout.panel.y) * stride_px +
                                 static_cast<std::size_t>(layout.panel.x);
    const std::uint64_t t0 = now_ns_();
    const HdBadgeView shown{st_.badge};
    const HdRect dirty = raster_->render(
        ask_.page, shown,
        std::span<std::uint16_t>{frame_.data() + origin_i, frame_.size() - origin_i}, stride_px);
    const std::uint64_t tr1 = now_ns_();
    st_.render_us_last = us_of(t0, tr1);
    if (st_.render_us_last > st_.render_us_max) st_.render_us_max = st_.render_us_last;
    panel_owed_ = false;

    HdRect panel_dirty = dirty;
    panel_dirty.x += layout.panel.x;
    panel_dirty.y += layout.panel.y;
    const HdRect clipped = clip_box(intersect(panel_dirty, box_of(layout.panel)),
                                    static_cast<std::int32_t>(s.w), static_cast<std::int32_t>(s.h));
    if (clipped.w > 0 && clipped.h > 0) {
        ledger_.mark(HdDirtyLedger::Region::Panel, clipped);
        if (asked && !pending_) {
            pending_ = true;
            pending_stamp_ = ask_.stamp_ns;
        }
    }
    place_(tr1);
    const std::uint64_t t1 = now_ns_();
    st_.panel_us_last = us_of(t0, t1);
    if (st_.panel_us_last > st_.panel_us_max) st_.panel_us_max = st_.panel_us_last;
    if (t1 >= t0 && t1 - t0 >= kDeadlineNs) deadline_ = true;
}

void HdRenderer::place_(std::uint64_t now) noexcept {

    const std::int64_t max_px = static_cast<std::int64_t>(ask_.surface.w) * ask_.surface.h / 4;
    bool wrote = false;
    for (std::uint8_t i = 0; i < own_.size(); ++i) {
        const HdRect box = ledger_.peek(i, HdDirtyLedger::Region::Panel);
        if (box.w <= 0 || box.h <= 0) continue;
        if (!owned_(i, now) && std::int64_t{box.w} * box.h > max_px) continue;
        copy_rect_(i, ledger_.take(i, HdDirtyLedger::Region::Panel));
        wrote = true;
    }
    if (wrote) slots_->handoff();
    if (pending_ && last_sent_ != kNoSlot) {
        const HdRect front = ledger_.peek(last_sent_, HdDirtyLedger::Region::Panel);
        if (front.w <= 0 || front.h <= 0) visible_(now_ns_(), false);
    }
}

bool HdRenderer::flip_(std::uint64_t now) noexcept {
    if (!flip_wanted_()) return false;
    const std::uint8_t o = flip_target_(now);
    if (o == kNoSlot || !owned_(o, now)) return false;
    place_(now);
    const HdRect game = ledger_.peek(o, HdDirtyLedger::Region::Game);
    if (game.w > 0 && game.h > 0) {
        if (job_.busy()) return false;
        job_.begin_copy(o, game, canvas_());
        return job_.busy();
    }
    const HdRect panel = ledger_.peek(o, HdDirtyLedger::Region::Panel);
    if (panel.w > 0 && panel.h > 0) return false;

    const HdSurface& s = ask_.surface;
    HdFlipChannel::Loan loan = std::move(own_[o]);
    loan->epoch = ask_.epoch;
    loan->seq = ++seq_;
    loan->view = FbView{.offset = s.slot[o],
                        .width = s.w,
                        .height = s.h,
                        .stride = s.stride,
                        .format = FbView::FbFormat::Rgb565};
    flips_->send(std::move(loan));
    last_sent_ = o;
    st_.state = HdOsdStatus::State::Showing;
    const std::uint64_t sent = now_ns_();
    if (slot_gen_[o] != shown_gen_ && slot_gen_[o] != fill_gen_ && slot_gen_[o] != 0) {
        vis_.backdrop();
        st_.bd_us_last = us_of(gen_started_, sent);
        if (st_.bd_us_last > st_.bd_us_max) st_.bd_us_max = st_.bd_us_last;
    }
    shown_gen_ = slot_gen_[o];
    if (pending_) visible_(sent, true);
    return true;
}

bool HdRenderer::slice_(std::uint64_t now) noexcept {
    switch (job_.step()) {
        case BackdropJob::Step::Scale: {
            const std::uint64_t t0 = now_ns_();
            const HdRect box = job_.scale_band();
            scale_ns_ += now_ns_() - t0;
            if (job_.busy()) return true;
            if (box.w > 0 && box.h > 0) ledger_.mark(HdDirtyLedger::Region::Game, box);
            ++canvas_gen_;
            gen_started_ = job_started_;
            bd_reset_ = false;
            st_.scale_us_last = us_of(0, scale_ns_);
            if (st_.scale_us_last > st_.scale_us_max) st_.scale_us_max = st_.scale_us_last;
            st_.sample_us_last = st_.read_us_last + st_.scale_us_last;
            if (st_.sample_us_last > st_.sample_us_max) st_.sample_us_max = st_.sample_us_last;
            scale_ns_ = 0;
            return true;
        }
        case BackdropJob::Step::Copy: {
            const std::uint8_t t = job_.target();
            if (!owned_(t, now)) return false;
            const std::uint64_t t0 = now_ns_();
            copy_rect_(t, job_.copy_band());
            copy_ns_ += now_ns_() - t0;
            if (job_.busy()) return true;
            (void)ledger_.take(t, HdDirtyLedger::Region::Game);
            slot_gen_[t & 1u] = canvas_gen_;
            slots_->handoff();
            st_.copy_us_last = us_of(0, copy_ns_);
            if (st_.copy_us_last > st_.copy_us_max) st_.copy_us_max = st_.copy_us_last;
            copy_ns_ = 0;
            return true;
        }
        case BackdropJob::Step::Idle:
            break;
    }
    if (!pacer_.due(now)) return false;
    pacer_.advance(now);
    ++ticks_;
    st_.skipped = static_cast<std::uint32_t>(pacer_.missed());
    if (!sampler_) return false;
    return read_(now);
}

bool HdRenderer::read_(std::uint64_t now) noexcept {
    job_started_ = now;
    const std::uint64_t t_read0 = now_ns_();
    const auto img = job_.read(*sampler_, static_cast<std::int64_t>(t_read0), canvas_(), bd_reset_);
    const std::uint64_t t_read1 = now_ns_();
    st_.read_us_last = us_of(t_read0, t_read1);
    if (st_.read_us_last > st_.read_us_max) st_.read_us_max = st_.read_us_last;
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
    if (img.live && img.same && !bd_reset_) ++st_.bd_same;
    if (!job_.busy()) {
        st_.sample_us_last = st_.read_us_last;
        if (st_.sample_us_last > st_.sample_us_max) st_.sample_us_max = st_.sample_us_last;
    }
    return job_.busy();
}

void HdRenderer::visible_(std::uint64_t now, bool by_flip) noexcept {
    if (pending_stamp_ != 0) vis_.sample(us_of(pending_stamp_, now));
    if (by_flip) {
        vis_.flipped();
    } else {
        vis_.in_place();
    }
    pending_ = false;
}

BackdropJob::Canvas HdRenderer::canvas_() noexcept {
    return BackdropJob::Canvas{.frame = std::span<std::uint16_t>{frame_},
                               .stride_px = ask_.surface.stride / 2u,
                               .game = box_of(layout_.game),
                               .cover = box_of(layout_.cover)};
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
        const auto* src = infra::bytes_of(frame_[y * stride_px + x]).data();
        hal::copy_to_uncached(dst.data() + y * s.stride + x * 2u, src,
                              static_cast<std::size_t>(rect.w) * 2u);
    }
    vis_.copied(static_cast<std::size_t>(rect.w) * 2u * static_cast<std::size_t>(rect.h));
}

}  // namespace mister::app
