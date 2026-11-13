// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "app/hd_osd_ask.h"
#include "app/hd_rect.h"
#include "app/picture_pixels.h"
#include "app/picture_row.h"
#include "app/px_plane.h"
#include "app/ratio.h"
#include "app/types.h"
#include "infra/indexed_span.h"
#include "infra/seat.h"
#include "svc/types.h"

namespace mister::app {

enum class LogoRotation : std::uint8_t { Upright, Left, Right };

struct FrontEndLogoKnobs {
    bool show = false;
    LogoRotation rotation = LogoRotation::Upright;
    bool direct_video = false;

    friend constexpr bool operator==(const FrontEndLogoKnobs&, const FrontEndLogoKnobs&) noexcept =
        default;
};

struct LogoPixel {

    enum class Slot : std::uint8_t { Red, Green, Blue, Alpha };

    static constexpr PixelBytes kBytes = PixelBytes::of(4);

    std::array<std::uint8_t, kBytes.v> v{};

    [[nodiscard]] constexpr ColorChannel sample(Slot slot) const noexcept {
        return ColorChannel{v[static_cast<std::size_t>(slot)]};
    }

    [[nodiscard]] constexpr bool clear() const noexcept {
        return sample(Slot::Alpha) == ColorChannel{};
    }

    friend constexpr bool operator==(LogoPixel, LogoPixel) = default;
};

struct LogoRun {
    std::span<const LogoPixel> v{};

    [[nodiscard]] constexpr LogoPixel at(PicturePixels column) const noexcept {
        return infra::IndexedSpan<const LogoPixel, PicturePixels>{v}[column];
    }
};

struct LogoArt {

    static constexpr PicturePixels kWidth{PicturePixels::of(480)};
    static constexpr PictureRows kRows{PictureRows::of(192)};

    std::array<LogoPixel, kWidth.count<std::size_t>() * kRows.count<std::size_t>()> v{};

    [[nodiscard]] constexpr std::span<const LogoPixel> pixels() const noexcept {
        return {v.data(), v.size()};
    }

    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;
};

extern const LogoArt kLogoArt;

extern const svc::Crc32 kLogoRgbaCrc32;
extern const char kLogoPngSha256[65];

[[nodiscard]] constexpr LogoRun run_of(const LogoArt& art, PictureRow row) noexcept {
    const std::size_t stride = LogoArt::kWidth.count<std::size_t>();
    const std::span<const LogoPixel> all = art.pixels();
    return LogoRun{all.subspan(static_cast<std::size_t>(row.v) * stride, stride)};
}

struct Logo565 {
    std::uint16_t v = 0;

    struct Levels {
        ColorChannel red{}, green{}, blue{};

        [[nodiscard]] Rgb888 under(LogoPixel pixel) const noexcept;
    };

    [[nodiscard]] Levels levels() const noexcept;

    [[nodiscard]] static Logo565 packed(Rgb888 colour) noexcept;

    [[nodiscard]] Logo565 under(LogoPixel pixel) const noexcept;

    void store_under(LogoPixel pixel, std::span<std::uint16_t> out) const noexcept {
        out.front() = under(pixel).v;
    }

    friend constexpr bool operator==(Logo565, Logo565) = default;
};

void lay_row(
    LogoRun run,
    std::span<std::uint16_t> out,
    PicturePixels from,
    PicturePixels across
) noexcept;

struct LogoOffset {
    std::int32_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr LogoOffset of(I n) noexcept {
        return LogoOffset{static_cast<std::int32_t>(n)};
    }

    friend constexpr bool operator==(LogoOffset, LogoOffset) = default;
};

struct LogoDrift {
    std::uint32_t v = 0;

    template <std::integral I>
    [[nodiscard]] static constexpr LogoDrift of(I n) noexcept {
        return LogoDrift{static_cast<std::uint32_t>(n)};
    }

    [[nodiscard]] static constexpr LogoDrift seeded_from(std::chrono::nanoseconds at) noexcept {
        constexpr std::uint32_t kSeed = 0x9E3779B9u;
        return LogoDrift{static_cast<std::uint32_t>(at.count()) ^ kSeed};
    }

    [[nodiscard]] constexpr LogoDrift next() const noexcept {
        constexpr unsigned kShiftA = 13;
        constexpr unsigned kShiftB = 17;
        constexpr unsigned kShiftC = 5;
        std::uint32_t x = v;
        x ^= x << kShiftA;
        x ^= x >> kShiftB;
        x ^= x << kShiftC;
        return LogoDrift{x};
    }

    [[nodiscard]] constexpr LogoOffset draw(LogoOffset span) const noexcept {
        return LogoOffset{static_cast<std::int32_t>(v % static_cast<std::uint32_t>(span.v))};
    }

    friend constexpr bool operator==(LogoDrift, LogoDrift) = default;
};

inline constexpr Ratio kLogoWidthShare{Ratio::Term::of(2), Ratio::Term::of(7)};
inline constexpr Ratio kLogoHalfShare{Ratio::Term::of(1), Ratio::Term::of(2)};
inline constexpr Ratio kLogoDarkWidthShare{Ratio::Term::of(1), Ratio::Term::of(4)};
inline constexpr Ratio kLogoUprightAspect{Ratio::Term::of(192), Ratio::Term::of(480)};
inline constexpr Ratio kLogoRotatedAspect{Ratio::Term::of(480), Ratio::Term::of(192)};

inline constexpr PictureRows kLogoDirectVideoRows{PictureRows::of(300)};

struct LogoBox {
    HdRect v{};

    [[nodiscard]] static constexpr LogoBox in(HdRect box, FrontEndLogoKnobs knobs) noexcept;

    [[nodiscard]] constexpr HdRect rect() const noexcept {
        return HdRect{.x = v.x, .y = v.y, .w = v.w, .h = v.h};
    }

    void paint(const LogoArt& art, PxPlane& canvas) const noexcept;
    friend constexpr bool operator==(LogoBox, LogoBox) = default;
};

constexpr LogoBox LogoBox::in(HdRect box, FrontEndLogoKnobs knobs) noexcept {
    if (box.is_empty())
        return {};
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t w = 0;
    std::int32_t h = 0;
    if (knobs.rotation != LogoRotation::Upright) {
        h = static_cast<std::int32_t>(
            kLogoHalfShare.applied_to<std::int32_t, std::uint32_t>(box.h)
        );
        w = static_cast<std::int32_t>(
            kLogoRotatedAspect.applied_to<std::int32_t, std::uint32_t>(h)
        );
        if (knobs.rotation == LogoRotation::Left) {
            y = box.h - h;
        } else {
            x = box.w - w;
        }
    } else {
        w = static_cast<std::int32_t>(
            kLogoWidthShare.applied_to<std::int32_t, std::uint32_t>(box.w)
        );
        h = static_cast<std::int32_t>(
            kLogoUprightAspect.applied_to<std::int32_t, std::uint32_t>(w)
        );
    }

    if (knobs.direct_video && PictureRows::of(box.h) < kLogoDirectVideoRows)
        h = static_cast<std::int32_t>(kLogoHalfShare.applied_to<std::int32_t, std::uint32_t>(h));
    return LogoBox{HdRect{.x = box.x + x, .y = box.y + y, .w = w, .h = h}};
}

struct LogoPick {
    LogoBox box{};
    LogoDrift drift{};
};

[[nodiscard]] LogoPick pick_spot(HdRect box, LogoDrift drift) noexcept;

class FrontEndLogo {
    TASTY_SEAT_RESIDENT(HdOsd);

public:
    static constexpr std::chrono::nanoseconds kMove{std::chrono::seconds{10}};

    void paint(
        PxPlane& canvas,
        HdRect box,
        FrontEndIdle idle,
        FrontEndLogoKnobs knobs,
        std::chrono::nanoseconds now
    ) noexcept;

    [[nodiscard]] bool move_due(FrontEndIdle idle, std::chrono::nanoseconds now) const noexcept;

    [[nodiscard]] HdRect dark_box() const noexcept { return pick_.box.rect(); }

private:
    LogoPick pick_{};
    LogoDrift drift_{};
    std::chrono::nanoseconds moved_{};
    bool dark_ = false;
};

}
