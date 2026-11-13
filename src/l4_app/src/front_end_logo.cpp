// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/front_end_logo.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "app/hd_theme.h"
#include "infra/byte_view.h"

namespace mister::app {

std::span<const std::uint8_t> LogoArt::bytes() const noexcept {
    const std::span<const LogoPixel> all = pixels();
    return infra::byte_cast<const std::uint8_t>(std::as_bytes(all));
}

Logo565::Levels Logo565::levels() const noexcept {
    constexpr unsigned kRedShift = 11;
    constexpr unsigned kGreenShift = 5;
    constexpr unsigned kFiveMask = 0x1Fu;
    constexpr unsigned kSixMask = 0x3Fu;
    constexpr unsigned kFiveRise = 3;
    constexpr unsigned kFiveBleed = 2;
    constexpr unsigned kSixRise = 2;
    constexpr unsigned kSixBleed = 4;
    const unsigned r5 = (v >> kRedShift) & kFiveMask;
    const unsigned g6 = (v >> kGreenShift) & kSixMask;
    const unsigned b5 = v & kFiveMask;
    return Levels{
        ColorChannel::of((r5 << kFiveRise) | (r5 >> kFiveBleed)),
        ColorChannel::of((g6 << kSixRise) | (g6 >> kSixBleed)),
        ColorChannel::of((b5 << kFiveRise) | (b5 >> kFiveBleed))
    };
}

Logo565 Logo565::packed(Rgb888 colour) noexcept {
    return Logo565{hd_pack565(colour)};
}

Logo565 Logo565::under(LogoPixel pixel) const noexcept {
    return packed(levels().under(pixel));
}

Rgb888 Logo565::Levels::under(LogoPixel pixel) const noexcept {
    constexpr unsigned kFull = 255;
    const ColorChannel alpha = pixel.sample(LogoPixel::Slot::Alpha);
    const unsigned keep = kFull - alpha.v;
    return rgb_from_channels(
        ColorChannel::of((pixel.sample(LogoPixel::Slot::Red).v * alpha.v + red.v * keep) / kFull),
        ColorChannel::of(
            (pixel.sample(LogoPixel::Slot::Green).v * alpha.v + green.v * keep) / kFull
        ),
        ColorChannel::of((pixel.sample(LogoPixel::Slot::Blue).v * alpha.v + blue.v * keep) / kFull)
    );
}

void lay_row(
    LogoRun run,
    std::span<std::uint16_t> out,
    PicturePixels from,
    PicturePixels across
) noexcept {
    for (std::size_t x = 0; x < out.size(); ++x) {
        const PicturePixels column = LogoArt::kWidth.sample_column(
            from + PicturePixels::of(x),
            across
        );
        const LogoPixel pixel = run.at(column);
        if (pixel.clear())
            continue;
        Logo565{out[x]}.store_under(pixel, out.subspan(x, 1));
    }
}

void LogoBox::paint(const LogoArt& art, PxPlane& canvas) const noexcept {
    const HdRect on = v.intersect(canvas.bounds());
    if (on.is_empty())
        return;
    const std::optional<PxPlane> plane = canvas.sub(on);
    if (!plane)
        return;
    const PicturePixels from = PicturePixels::of(on.x - v.x);
    const PicturePixels across = PicturePixels::of(v.w);
    const std::size_t columns = PicturePixels::of(on.w).count<std::size_t>();
    for (std::int32_t y = 0; y < on.h; ++y) {
        const PictureRow row = art.kRows.sample_row(
            PictureRows::of(on.y + y - v.y),
            PictureRows::of(v.h)
        );
        lay_row(run_of(art, row), plane->row(PlaneRow{.v = y}).first(columns), from, across);
    }
}

LogoPick pick_spot(HdRect box, LogoDrift drift) noexcept {
    const std::int32_t w = static_cast<std::int32_t>(
        kLogoDarkWidthShare.applied_to<std::int32_t, std::uint32_t>(box.w)
    );
    const std::int32_t h = static_cast<std::int32_t>(
        kLogoUprightAspect.applied_to<std::int32_t, std::uint32_t>(w)
    );
    if (w <= 0 || h <= 0 || w > box.w || h > box.h)
        return {};
    const std::int32_t span_x = box.w - w;
    const std::int32_t span_y = box.h - h;
    LogoDrift state = drift;
    LogoOffset x{};
    if (span_x > 0) {
        state = state.next();
        x = state.draw(LogoOffset::of(span_x));
    }
    LogoOffset y{};
    if (span_y > 0) {
        state = state.next();
        y = state.draw(LogoOffset::of(span_y));
    }
    return LogoPick{LogoBox{HdRect{.x = box.x + x.v, .y = box.y + y.v, .w = w, .h = h}}, state};
}

void FrontEndLogo::paint(
    PxPlane& canvas,
    HdRect box,
    FrontEndIdle idle,
    FrontEndLogoKnobs knobs,
    std::chrono::nanoseconds now
) noexcept {
    TASTY_SEAT_BODY(FrontEndLogo);
    if (idle == FrontEndIdle::DarkLogo) {
        if (!dark_ || now - moved_ >= kMove) {
            if (!dark_)
                drift_ = LogoDrift::seeded_from(now);
            dark_ = true;
            moved_ = now;
            pick_ = pick_spot(box, drift_);
            drift_ = pick_.drift;
        }
        pick_.box.paint(kLogoArt, canvas);
        return;
    }
    dark_ = false;
    if (idle != FrontEndIdle::Awake || !knobs.show)
        return;
    LogoBox::in(box, knobs).paint(kLogoArt, canvas);
}

bool FrontEndLogo::move_due(FrontEndIdle idle, std::chrono::nanoseconds now) const noexcept {
    return idle == FrontEndIdle::DarkLogo && (!dark_ || now - moved_ >= kMove);
}

}
