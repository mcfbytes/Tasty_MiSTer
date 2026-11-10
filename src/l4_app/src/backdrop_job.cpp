// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/backdrop_job.h"

#include "app/rgb_rows.h"

namespace mister::app {
namespace {

[[nodiscard]] HdRect canvas_game(const BackdropJob::Canvas& c) noexcept {
    if (c.frame.row_px == 0 || c.frame.pixels.empty())
        return {};
    return c.game.intersect(c.frame.bounds());
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
    const bool usable = img.width != 0 && img.rgb.covers(rgb_row_of(img.width), img.rgb.rows);
    const HdRect game = canvas_game(c);
    if (!usable || game.is_empty())
        return img;
    img_ = img;
    canvas_ = c;
    box_ = game.union_with(c.glass);
    next_row_ = box_.y;
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
        canvas_.frame,
        canvas_.game,
        canvas_.picture,
        next_row_,
        y1
    );
    for (const BackdropScalerTarget& t : canvas_.targets)
        scaler_.scale_target_rows(img_.rgb, img_.width, canvas_.picture, t, next_row_, y1);
    next_row_ = y1;
    if (next_row_ < end)
        return {};
    step_ = Step::Idle;
    whole_ = true;
    img_ = {};
    return box_;
}

void BackdropJob::begin_copy(FlipBuffer buffer, HdRect box, const Canvas& c) noexcept {
    TASTY_SEAT_BODY(BackdropJob);
    if (busy() || !whole_)
        return;
    const HdRect clipped = canvas_game(Canvas{.frame = c.frame, .game = box});
    if (clipped.is_empty())
        return;
    canvas_ = c;
    box_ = clipped;
    next_row_ = clipped.y;
    target_ = buffer;
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
