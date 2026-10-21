// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/backdrop_job.h"

namespace mister::app {
namespace {

[[nodiscard]] HdRect clip_box(HdRect r, std::int32_t w, std::int32_t h) noexcept {
    if (r.w <= 0 || r.h <= 0)
        return {};
    if (r.x < 0) {
        r.w += r.x;
        r.x = 0;
    }
    if (r.y < 0) {
        r.h += r.y;
        r.y = 0;
    }
    if (r.w <= 0 || r.h <= 0 || r.x >= w || r.y >= h)
        return {};
    if (r.x + r.w > w)
        r.w = w - r.x;
    if (r.y + r.h > h)
        r.h = h - r.y;
    return r;
}

[[nodiscard]] HdRect canvas_game(const BackdropJob::Canvas& c) noexcept {
    if (c.stride_px == 0 || c.frame.empty())
        return {};
    return clip_box(
        c.game,
        static_cast<std::int32_t>(c.stride_px),
        static_cast<std::int32_t>(c.frame.size() / c.stride_px)
    );
}

}

BackdropSampler::Image BackdropJob::read(
    BackdropSampler& sampler,
    std::int64_t now,
    const Canvas& c,
    bool reset
) noexcept {
    TASTY_SEAT_BODY(BackdropJob);
    if (busy())
        return {};
    const BackdropSampler::Image img = sampler.poll(now, true);
    if (img.stuck || !img.live)
        return img;
    if (!(img.fresh && !img.same) && !reset)
        return img;
    const bool usable = img.width != 0 && img.height != 0 && img.line >= img.width * 3u;
    const HdRect game = canvas_game(c);
    if (!usable || game.w <= 0 || game.h <= 0)
        return img;
    img_ = img;
    canvas_ = c;
    box_ = game;
    next_row_ = game.y;
    step_ = Step::Scale;
    return img;
}

HdRect BackdropJob::scale_band() noexcept {
    TASTY_SEAT_BODY(BackdropJob);
    if (step_ != Step::Scale)
        return {};
    const std::int32_t end = box_.y + box_.h;
    const std::int32_t y1 = next_row_ + kBandRows < end ? next_row_ + kBandRows : end;
    scaler_.scale_rows(
        img_.rgb,
        img_.width,
        img_.height,
        img_.line,
        canvas_.frame,
        canvas_.stride_px,
        canvas_.game,
        canvas_.cover,
        next_row_,
        y1
    );
    next_row_ = y1;
    if (next_row_ < end)
        return {};
    step_ = Step::Idle;
    whole_ = true;
    img_ = {};
    return box_;
}

void BackdropJob::begin_copy(std::uint8_t slot, HdRect box, const Canvas& c) noexcept {
    TASTY_SEAT_BODY(BackdropJob);
    if (busy() || !whole_)
        return;
    const HdRect clipped = canvas_game(Canvas{c.frame, c.stride_px, box, {}});
    if (clipped.w <= 0 || clipped.h <= 0)
        return;
    canvas_ = c;
    box_ = clipped;
    next_row_ = clipped.y;
    slot_ = slot;
    step_ = Step::Copy;
}

HdRect BackdropJob::copy_band() noexcept {
    TASTY_SEAT_BODY(BackdropJob);
    if (step_ != Step::Copy)
        return {};
    const std::int32_t end = box_.y + box_.h;
    const std::int32_t y1 = next_row_ + kBandRows < end ? next_row_ + kBandRows : end;
    const HdRect band{box_.x, next_row_, box_.w, y1 - next_row_};
    next_row_ = y1;
    if (next_row_ >= end)
        step_ = Step::Idle;
    return band;
}

void BackdropJob::abort() noexcept {
    TASTY_SEAT_BODY(BackdropJob);
    if (step_ == Step::Scale)
        whole_ = false;
    step_ = Step::Idle;
    img_ = {};
    box_ = {};
}

void BackdropJob::reset_canvas() noexcept {
    TASTY_SEAT_BODY(BackdropJob);
    abort();
    whole_ = true;
}

}
