// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hd_renderer.h"

#include <algorithm>
#include <chrono>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "app/hd_layout.h"
#include "app/hd_theme.h"
#include "app/hd_themes.h"
#include "app/log_clock.h"
#include "hal/scaler_buffers.h"

namespace mister::app {
namespace {

[[nodiscard]] StatusMicros span_us(std::uint64_t from, std::uint64_t to) noexcept {
    return us_of(std::chrono::nanoseconds{to} - std::chrono::nanoseconds{from});
}

[[nodiscard]] bool same_view(const HdBadgeView& a, const HdBadgeView& b) noexcept {
    return a.badge == b.badge && a.slots == b.slots;
}

}

HdRenderer::HdRenderer(const Wiring& w, std::optional<Frame> frame, std::uint8_t fps) noexcept
    : w_(w),
      pacer_(fps) {
    if (!frame)
        return;
    holds_.emplace(frame->flips);
    slots_ = std::move(frame->mem);
    rasters_ = std::move(frame->rasters);
}

void HdRenderer::bind_scaler(hal::ScalerBuffers window) noexcept {
    scaler_uio_ = window.uio_bound();
    sampler_.emplace(std::move(window));
}

void HdRenderer::set_fps(std::uint8_t fps) noexcept {
    pacer_.set_fps(fps);
    st_.fps = pacer_.fps();
}

void HdRenderer::set_look(HdLook look) noexcept {
    look_ = hd_look_index(look);
    theme_ = hd_theme_tokens(look_);
}

void HdRenderer::begin() noexcept {
    TASTY_SEAT_BODY(HdRenderer);
    up_ = true;
    st_ = {};
    st_.fps = pacer_.fps();
    st_.slots_uio = slots_ && slots_->uio_bound();
    st_.scaler_uio = scaler_uio_;
    if (raster_() || sampler_) {
        scaler_.open(theme_);
        if (raster_()) {
            if (!raster_()->open(theme_, HdLayout{})) {
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
    if (!up_ || st_.state == HdOsdStatus::State::Failed)
        return;

    const bool pixels = holds_ && slots_ && raster_open_;
    const std::uint64_t now = now_ns_();
    if (pixels)
        holds_->collect(w_.clock.now());

    bool sampled = false;
    HdOsdAsk got{};
    if (reader_.take_if_changed(w_.ask, got)) {
        const bool opened = got.open && !ask_.open;
        const bool closed = !got.open && ask_.open;
        ask_ = got;
        st_.badge = got.badge;
        if (opened) {
            pacer_.resync();
            if (sampler_)
                sampler_->restart(static_cast<std::int64_t>(now));
        }
        if (closed)
            close_job_();
        const HdLook want = hd_look_index(got.look);

        const bool glass = want.opacity != look_.opacity
            || (want.display != look_.display && want.opacity < kHdOpacityMax);
        if ((want.layout != look_.layout || want.colors != look_.colors || glass)
            && !retheme_(want))
            return;
        look_.display = want.display;
        sampled = true;
    }
    bool rolled = false;
    if (const auto w = vis_.roll(now)) {
        st_.window = *w;
        rolled = true;
    }

    if (!ask_.open) {
        st_.state = HdOsdStatus::State::Ready;
        st_.fail = HdOsdStatus::Fail::None;
        st_.fps = pacer_.fps();
        if (sampled || rolled)
            publish_();
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
            if (sampled || rolled)
                publish_();
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
    draw_();
}

bool HdRenderer::idle() const noexcept {
    const std::uint32_t g = w_.ask.generation();
    if (g != 0 && g != reader_.seen())
        return false;
    if (!up_ || !ask_.open || st_.state == HdOsdStatus::State::Failed)
        return true;
    const std::uint64_t now = now_ns_();
    if (sampler_ && sampler_->poll_due(static_cast<std::int64_t>(now)))
        return false;
    const bool pixels = holds_ && slots_ && raster_open_;
    if (pixels && holds_->returning())
        return false;
    if (!pixels || !epoch_valid_)
        return !pacer_.due(now);
    if (card_() && panel_owed_())
        return false;
    return !can_progress_(now);
}

infra::ParkTimeout HdRenderer::park_ms() const noexcept {
    constexpr infra::ParkTimeout kIdlePoll{1000};
    if (!up_ || !ask_.open || st_.state == HdOsdStatus::State::Failed)
        return kIdlePoll;
    const std::uint64_t now = now_ns_();
    const bool pixels = holds_ && slots_ && raster_open_;
    infra::ParkTimeout ms = pixels && job_.busy() ? kIdlePoll : pacer_.park_ms(now);
    if (pixels && epoch_valid_) {
        if (const auto wait = wait_ns_(w_.clock.now())) {
            using Ms = std::chrono::duration<std::uint64_t, std::milli>;
            const Ms wait_ms = std::chrono::ceil<Ms>(*wait);
            if (wait_ms < ms)
                ms = std::chrono::duration_cast<infra::ParkTimeout>(wait_ms);
        }
    }
    if (sampler_ && sampler_->probing()) {
        const infra::ParkTimeout probe = sampler_->park_ms(static_cast<std::int64_t>(now));
        if (probe < ms)
            ms = probe;
    }
    return ms;
}

void HdRenderer::release() noexcept {
    TASTY_SEAT_BODY(HdRenderer);
    if (holds_)
        holds_->release_all();

    for (std::unique_ptr<HdRaster>& raster : rasters_) {
        if (raster)
            raster->close();
    }
    raster_open_ = false;
    close_job_();
    up_ = false;
    ask_.open = false;
    st_.state = HdOsdStatus::State::Off;
    publish_();
}

void HdRenderer::publish_() noexcept {
    w_.status.publish(st_);
}

std::uint64_t HdRenderer::now_ns_() const noexcept {
    const auto n = w_.clock.now().time_since_epoch().count();
    return n < 0 ? 0 : static_cast<std::uint64_t>(n);
}

bool HdRenderer::ask_moved_() const noexcept {
    const std::uint32_t g = w_.ask.generation();
    return g != 0 && g != reader_.seen();
}

bool HdRenderer::panel_asked_() const noexcept {
    return !drawn_ || drawn_->gen != AskGen{reader_.seen()};
}

bool HdRenderer::panel_owed_() const noexcept {
    if (panel_asked_())
        return true;
    return !same_view(HdBadgeView{st_.badge, ask_.slots}, drawn_->view);
}

bool HdRenderer::canvas_fresh_() const noexcept {
    return !drawn_ || drawn_->fill != fill_gen_;
}

bool HdRenderer::stale_(FlipBuffer buffer) const noexcept {
    const HdRect g = ledger_.peek(buffer, HdDirtyLedger::Region::Game);
    return !g.is_empty() || cards_stale_(buffer);
}

bool HdRenderer::cards_stale_(FlipBuffer buffer) const noexcept {
    for (const HdDirtyLedger::Region r : kCards) {
        const HdRect box = ledger_.peek(buffer, r);
        if (!box.is_empty())
            return true;
    }
    return false;
}

bool HdRenderer::flip_wanted_() const noexcept {
    if (!holds_)
        return false;
    const auto sent = holds_->last_sent();
    return !sent || stale_(*sent);
}

std::optional<FlipBuffer> HdRenderer::flip_target_(HdFlipHolds::Instant now) const noexcept {
    if (const auto sent = holds_->last_sent())
        return sent->other();
    for (const FlipBuffer b : kFlipBuffers)
        if (holds_->owned(b, now) && !stale_(b))
            return b;
    if (job_.step() == BackdropJob::Step::Copy)
        return job_.target();
    for (const FlipBuffer b : kFlipBuffers)
        if (holds_->owned(b, now))
            return b;
    return std::nullopt;
}

bool HdRenderer::can_progress_(std::uint64_t now) const noexcept {
    switch (job_.step()) {
        case BackdropJob::Step::Scale:
            return true;
        case BackdropJob::Step::Copy:
            return holds_->owned(job_.target(), w_.clock.now());
        case BackdropJob::Step::Idle:
            break;
    }
    if (pacer_.due(now))
        return true;
    if (!flip_wanted_())
        return false;
    const auto at = w_.clock.now();
    const auto o = flip_target_(at);
    if (!o || !holds_->owned(*o, at))
        return false;
    const HdRect g = ledger_.peek(*o, HdDirtyLedger::Region::Game);
    return g.is_empty() || job_.committed();
}

std::optional<HdFlipHolds::Span> HdRenderer::wait_ns_(HdFlipHolds::Instant now) const noexcept {
    std::optional<HdFlipHolds::Span> best{};
    const auto consider = [&](FlipBuffer b) noexcept {
        const auto until = holds_->latched_until(b, now);
        if (until && (!best || *until - now < *best))
            best = *until - now;
    };
    if (job_.step() == BackdropJob::Step::Copy)
        consider(job_.target());
    if (job_.step() == BackdropJob::Step::Idle && flip_wanted_()) {
        if (const auto sent = holds_->last_sent()) {
            consider(sent->other());
        } else {
            for (const FlipBuffer b : kFlipBuffers)
                consider(b);
        }
    }
    return best;
}

bool HdRenderer::ensure_frame_() noexcept {
    const HdSurface& s = ask_.surface;
    const std::size_t bytes = s.frame_bytes();
    const bool stride_ok = s.stride_ok();
    const bool fits = slots_
        && bytes <= kMaxFrameBytes
        && static_cast<std::size_t>(s.slot[0]) + bytes <= slots_->region().len
        && static_cast<std::size_t>(s.slot[1]) + bytes <= slots_->region().len;
    if (s.size().empty() || !stride_ok || !fits) {
        st_.state = HdOsdStatus::State::Failed;
        st_.fail = HdOsdStatus::Fail::NoSlotWindow;
        publish_();
        return false;
    }
    const std::size_t px = s.pixel_count();
    if (frame_.size() != px)
        frame_.assign(px, 0);
    return true;
}

void HdRenderer::close_job_() noexcept {
    job_.abort();
    epoch_valid_ = false;
    pending_ = false;
    drawn_.reset();
}

std::unique_ptr<HdRaster>& HdRenderer::raster_() noexcept {
    return rasters_[static_cast<std::size_t>(hd_layout_decl(look_.layout).tmpl)];
}

bool HdRenderer::retheme_(HdLook look) noexcept {
    std::unique_ptr<HdRaster>& was = raster_();
    set_look(look);
    close_job_();
    scaler_.open(theme_);
    if (!raster_open_)
        return true;
    if (&raster_() != &was)
        was->close();
    if (raster_() && raster_()->open(theme_, layout_))
        return true;
    raster_open_ = false;
    st_.state = HdOsdStatus::State::Failed;
    st_.fail = HdOsdStatus::Fail::RasterInit;
    publish_();
    return false;
}

void HdRenderer::paint_canvas_() noexcept {
    bg_seen_ = ask_.bg;
    idle_seen_ = ask_.idle;
    shows_seen_ = ask_.shows;
    PxPlane canvas = frame_plane_();
    const FrontEndPattern* pattern = pattern_for_(ask_.bg);
    if (!pattern) {
        const std::uint16_t fill = hd_pack565(theme_.primary);
        std::fill(frame_.begin(), frame_.end(), fill);
        return;
    }
    const HdRect box = ask_.surface.bounds();
    black_.paint(canvas, canvas.bounds());
    const bool dark = ask_.idle == FrontEndIdle::Dark || ask_.idle == FrontEndIdle::DarkLogo;
    if (!dark)
        pattern->paint(canvas, box);
    logo_.paint(canvas, box, ask_.idle, logo_knobs_, std::chrono::nanoseconds{now_ns_()});
    curtain(canvas, box, ask_.idle);
}

void HdRenderer::load_wallpaper_() noexcept {
    wallpaper_label_ = ask_.ini_label;
    if (!w_.vfs)
        return;
    wallpaper_.load(*w_.vfs, ask_.ini_label.view());
}

const FrontEndPattern* HdRenderer::pattern_for_(FrontEndBg bg) const noexcept {
    switch (bg) {
        case FrontEndBg::None:
            return nullptr;
        case FrontEndBg::Wallpaper:
            return &wallpaper_;
        case FrontEndBg::HBars1:
            return &hbars1_;
        case FrontEndBg::HBars2:
            return &hbars2_;
        case FrontEndBg::VBars1:
            return &vbars1_;
        case FrontEndBg::VBars2:
            return &vbars2_;
        case FrontEndBg::Spectrum:
            return &spectrum_;
        case FrontEndBg::Black:
            return &black_;
    }
    return nullptr;
}

bool HdRenderer::card_() const noexcept {
    return ask_.idle == FrontEndIdle::Awake && ask_.shows == HdOsdShows::Menu;
}

bool HdRenderer::samples_() const noexcept {
    return card_() && !paints_pattern(ask_.bg);
}

void HdRenderer::start_epoch_(const HdLayout& layout) noexcept {
    epoch_seen_ = ask_.epoch;
    epoch_valid_ = true;
    layout_ = layout;
    glass_.relayout(theme_, layout);
    ledger_.reset(layout.panel, layout.band, hd_beside_panel(layout));
    paint_canvas_();
    job_.reset_canvas();
    seq_ = {};
    if (holds_)
        holds_->new_epoch();
    canvas_gen_ = canvas_gen_.next();
    fill_gen_ = canvas_gen_;
    shown_gen_ = {};
    drawn_.reset();
    pacer_.resync();
    if (raster_())
        raster_()->relayout(layout);
}

void HdRenderer::draw_() noexcept {
    if (!ensure_frame_())
        return;
    const HdSurface& s = ask_.surface;
    const HdLayout layout = hd_page_layout(
        hd_template_traits(hd_layout_decl(look_.layout).tmpl),
        ask_.page,
        s.size(),
        hd_game_display_of(look_)
    );
    const std::optional<PxPlane> window = frame_plane_().sub(hd_menu_box(layout));
    if (!window) {
        st_.state = HdOsdStatus::State::Failed;
        st_.fail = HdOsdStatus::Fail::NoSlotWindow;
        publish_();
        return;
    }

    const bool wallpaper_edge = ask_.bg == FrontEndBg::Wallpaper
        && (ask_.bg != bg_seen_ || ask_.ini_label != wallpaper_label_);
    if (wallpaper_edge)
        load_wallpaper_();
    if (!epoch_valid_
        || ask_.epoch != epoch_seen_
        || layout != layout_
        || ask_.bg != bg_seen_
        || ask_.idle != idle_seen_
        || ask_.shows != shows_seen_
        || wallpaper_edge
        || logo_.move_due(ask_.idle, std::chrono::nanoseconds{now_ns_()}))
        start_epoch_(layout);
    deadline_ = false;
    st_.fps = pacer_.fps();

    const bool card = card_();
    const bool asked = panel_asked_();
    if (panel_owed_() && card) {
        panel_step_(layout, *window, asked);
    } else if (!card) {

        bool ready = !canvas_fresh_();
        if (canvas_fresh_()) {
            const auto at = w_.clock.now();
            for (const FlipBuffer b : kFlipBuffers) {
                if (!holds_->owned(b, at))
                    continue;
                copy_rect_(b, s.bounds(), false);
                for (const HdDirtyLedger::Region r :
                     {HdDirtyLedger::Region::Panel,
                      HdDirtyLedger::Region::Band,
                      HdDirtyLedger::Region::Game})
                    (void)ledger_.take(b, r);
                ready = true;
            }
            if (ready)
                slots_->handoff();
        }
        if (ready)
            drawn_ = PanelDrawn{
                AskGen{reader_.seen()},
                HdBadgeView{st_.badge, ask_.slots},
                fill_gen_
            };
    } else {
        place_(w_.clock.now());
    }

    const std::uint64_t t0 = now_ns_();
    for (;;) {
        const bool sliced = slice_(now_ns_());
        if (st_.state == HdOsdStatus::State::Failed)
            return;
        const bool flipped = flip_(w_.clock.now());
        if (!sliced && !flipped)
            break;
        if (panel_owed_() || ask_moved_() || now_ns_() - t0 >= kSliceNs)
            break;
    }
    if (st_.state == HdOsdStatus::State::Failed)
        return;
    if (deadline_) {
        st_.state = HdOsdStatus::State::Failed;
        st_.fail = HdOsdStatus::Fail::Deadline;
    } else {
        if (st_.state != HdOsdStatus::State::Showing)
            st_.state = HdOsdStatus::State::Ready;
        st_.fail = HdOsdStatus::Fail::None;
    }
    publish_();
}

void HdRenderer::panel_step_(const HdLayout& layout, PxPlane panel, bool asked) noexcept {
    const HdSurface& s = ask_.surface;
    const HdRect menu = hd_menu_box(layout);
    const std::uint64_t t0 = now_ns_();
    const HdBadgeView shown{st_.badge, ask_.slots};
    const auto render = [&] {
        return raster_()->render(ask_.page, shown, panel);
    };
    HdDirty dirty = render();

    if (!canvas_fresh_() && spills_(dirty, layout)) {
        start_epoch_(layout);
        dirty = render();
    }
    const std::uint64_t tr1 = now_ns_();
    st_.render_us.note(span_us(t0, tr1));
    drawn_ = PanelDrawn{AskGen{reader_.seen()}, shown, fill_gen_};

    struct Box {
        HdDirtyLedger::Region region = HdDirtyLedger::Region::Panel;
        HdRect r{};
    };

    std::array<Box, kCards.size() * HdDirty::kMaxRects> boxes{};
    std::uint8_t n = 0;
    bool each_fits = true;
    const PixelCount max_px = in_place_px_();
    for (std::uint8_t i = 0; i < dirty.count && i < dirty.rects.size(); ++i) {
        const HdRect drawn = dirty.rects[i].placed_in(menu);
        for (const HdDirtyLedger::Region region : kCards) {
            const HdRect card = region == HdDirtyLedger::Region::Band ? layout.band : layout.panel;
            const HdRect clipped = drawn.intersect(card).intersect(s.bounds());
            if (clipped.is_empty())
                continue;
            if (pixels_of(clipped) > max_px)
                each_fits = false;
            boxes[n++] = Box{region, clipped};
        }
    }
    if (n > 0 && asked && !pending_) {
        pending_ = true;
        pending_stamp_ = ask_.stamp_ns;
    }
    bool wrote = false;
    const auto at = w_.clock.now();
    for (std::uint8_t i = 0; i < n; ++i) {
        ledger_.mark(boxes[i].region, boxes[i].r);
        if (each_fits)
            wrote = copy_stale_(at) || wrote;
    }
    place_(at, wrote);
    const std::uint64_t t1 = now_ns_();
    st_.panel_us.note(span_us(t0, t1));
    if (t1 >= t0 && t1 - t0 >= kDeadlineNs)
        deadline_ = true;
}

PixelCount HdRenderer::in_place_px_() const noexcept {

    return ask_.surface.size().area().quarter();
}

bool HdRenderer::copy_stale_(HdFlipHolds::Instant at) noexcept {
    const PixelCount max_px = in_place_px_();
    bool wrote = false;
    for (const FlipBuffer b : kFlipBuffers) {
        for (const HdDirtyLedger::Region r : kCards) {
            const HdRect box = ledger_.peek(b, r);
            if (box.is_empty())
                continue;
            if (!holds_->owned(b, at) && pixels_of(box) > max_px)
                continue;
            copy_rect_(b, ledger_.take(b, r));
            wrote = true;
        }
    }
    return wrote;
}

bool HdRenderer::spills_(const HdDirty& dirty, const HdLayout& layout) const noexcept {
    if (layout.band.is_empty())
        return false;
    const HdRect menu = hd_menu_box(layout);
    for (std::uint8_t i = 0; i < dirty.count && i < dirty.rects.size(); ++i) {
        const HdRect drawn = dirty.rects[i].placed_in(menu);
        const HdRect over = drawn.intersect(layout.game);
        if (!over.is_empty())
            return true;
    }
    return false;
}

void HdRenderer::place_(HdFlipHolds::Instant at, bool wrote) noexcept {
    if (copy_stale_(at) || wrote)
        slots_->handoff();
    if (const auto sent = holds_->last_sent(); pending_ && sent && !cards_stale_(*sent))
        visible_(now_ns_(), false);
}

bool HdRenderer::flip_(HdFlipHolds::Instant at) noexcept {
    if (!flip_wanted_())
        return false;
    const auto target = flip_target_(at);
    if (!target || !holds_->owned(*target, at))
        return false;
    const FlipBuffer o = *target;
    place_(at);
    const HdRect game = ledger_.peek(o, HdDirtyLedger::Region::Game);
    if (!game.is_empty()) {
        if (job_.busy())
            return false;
        job_.begin_copy(o, game, canvas_());
        return job_.busy();
    }
    if (cards_stale_(o))
        return false;

    const HdSurface& s = ask_.surface;
    const FlipSeq next = seq_.next();
    const HdFlipSlot slot{
        .epoch = ask_.epoch,
        .seq = next,
        .view = FbView{
            .offset = o.in(s.slot),
            .size = PictureSize{PicturePixels::of(s.w), PictureRows::of(s.h)},
            .stride = s.stride,
            .format = FbView::FbFormat::Rgb565
        }
    };
    if (!holds_->lend(o, at, slot))
        return false;
    seq_ = next;
    st_.state = HdOsdStatus::State::Showing;
    const std::uint64_t sent = now_ns_();
    const CanvasGen gen = holds_->gen(o);
    if (gen != shown_gen_ && gen != fill_gen_ && gen != CanvasGen{}) {
        vis_.backdrop();
        st_.bd_us.note(span_us(gen_started_, sent));
    }
    shown_gen_ = gen;
    if (pending_)
        visible_(sent, true);
    return true;
}

bool HdRenderer::slice_(std::uint64_t now) noexcept {
    switch (job_.step()) {
        case BackdropJob::Step::Scale: {
            const std::uint64_t t0 = now_ns_();
            const HdRect box = job_.scale_band();
            scale_ns_ += now_ns_() - t0;
            if (job_.busy())
                return true;
            if (!box.is_empty())
                ledger_.mark(HdDirtyLedger::Region::Game, box);
            canvas_gen_ = canvas_gen_.next();
            gen_started_ = job_started_;
            st_.scale_us.note(span_us(0, scale_ns_));
            st_.sample_us.note(st_.read_us.last + st_.scale_us.last);
            scale_ns_ = 0;
            return true;
        }
        case BackdropJob::Step::Copy: {
            const FlipBuffer t = job_.target();
            if (!holds_->owned(t, w_.clock.now()))
                return false;
            const std::uint64_t t0 = now_ns_();
            copy_rect_(t, job_.copy_band());
            copy_ns_ += now_ns_() - t0;
            if (job_.busy())
                return true;
            (void)ledger_.take(t, HdDirtyLedger::Region::Game);
            holds_->set_gen(t, canvas_gen_);
            slots_->handoff();
            st_.copy_us.note(span_us(0, copy_ns_));
            copy_ns_ = 0;
            return true;
        }
        case BackdropJob::Step::Idle:
            break;
    }
    if (!pacer_.due(now))
        return false;
    pacer_.advance(now);
    ++ticks_;
    st_.skipped = static_cast<std::uint32_t>(pacer_.missed());

    if (!sampler_ || layout_.game.is_empty() || !samples_())
        return false;
    return read_(now);
}

bool HdRenderer::read_(std::uint64_t now) noexcept {
    job_started_ = now;

    const bool refilled = canvas_gen_ == fill_gen_;
    const std::uint64_t t_read0 = now_ns_();
    const auto img = job_.read(*sampler_, static_cast<std::int64_t>(t_read0), canvas_(), refilled);
    const std::uint64_t t_read1 = now_ns_();
    st_.read_us.note(span_us(t_read0, t_read1));
    if (img.stuck) {
        st_.state = HdOsdStatus::State::Failed;
        st_.fail = HdOsdStatus::Fail::PortStuck;
        st_.fps = pacer_.fps();
        publish_();
        return false;
    }
    st_.backdrop_w = img.live ? img.width : 0;
    st_.backdrop_h = img.live ? img.rgb.rows : PictureRows{};
    st_.stride_mib = img.stride_mib;
    st_.lowlat = img.lowlat;
    if (img.live && img.same && !refilled)
        ++st_.bd_same;
    if (!job_.busy()) {
        st_.sample_us.note(st_.read_us.last);
    }
    return job_.busy();
}

void HdRenderer::visible_(std::uint64_t now, bool by_flip) noexcept {
    if (pending_stamp_ != 0)
        vis_.sample(span_us(pending_stamp_, now));
    if (by_flip) {
        vis_.flipped();
    } else {
        vis_.in_place();
    }
    pending_ = false;
}

PxPlane HdRenderer::frame_plane_() noexcept {
    return PxPlane{.pixels = std::span<std::uint16_t>{frame_}, .row_px = ask_.surface.row_len()};
}

BackdropJob::Canvas HdRenderer::canvas_() noexcept {
    return BackdropJob::Canvas{
        .frame = frame_plane_(),
        .game = layout_.game,
        .picture = layout_.picture,
        .targets = glass_.targets(),
        .glass = glass_.bounds()
    };
}

void HdRenderer::copy_rect_(FlipBuffer buffer, HdRect rect, bool compose) noexcept {
    const HdSurface& s = ask_.surface;
    if (!s.bounds().contains(rect))
        return;
    const std::optional<PxPlane> box = frame_plane_().sub(rect);
    if (!box)
        return;
    const std::size_t bytes = s.frame_bytes();
    std::span<std::byte> dst = slots_->view(buffer.in(s.slot), bytes);
    const auto put = [&](std::int32_t x, std::int32_t y, std::span<const std::uint16_t> px) {
        const std::size_t at = s.byte_offset(
            static_cast<std::uint32_t>(x),
            static_cast<std::uint32_t>(y)
        );
        hal::copy_wide(dst.subspan(at), std::as_bytes(px));
    };

    const bool glass = compose && card_();
    for (std::int32_t y = 0; y < rect.h; ++y) {
        const auto row = box->row(PlaneRow{y}).first(static_cast<std::size_t>(rect.w));
        const std::int32_t at_y = rect.y + y;
        const HdRect g = glass ? glass_.span_at(at_y, rect.x, rect.x + rect.w) : HdRect{};
        if (g.is_empty()) {
            put(rect.x, at_y, row);
            continue;
        }
        const auto left = static_cast<std::size_t>(g.x - rect.x);
        const auto mid = static_cast<std::size_t>(g.w);
        if (left > 0)
            put(rect.x, at_y, row.first(left));
        put(g.x, at_y, glass_.compose(row.subspan(left, mid), g.x, at_y));
        if (left + mid < row.size())
            put(g.x + g.w, at_y, row.subspan(left + mid));
    }
    vis_.copied(
        static_cast<std::size_t>(rect.w) * kRgb565PixelBytes * static_cast<std::size_t>(rect.h)
    );
}

}
