// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_splash.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

#include "infra/endian.h"
#include "os/mmio_region.h"

#include "app/fb_view.h"
#include "app/picture_size.h"
#include "app/video_pump.h"
#include "hal/phys_region.h"
#include "svc/video_service.h"

#include <zlib.h>

#include "splash_embed.h"

namespace mister::fw {

using ::mister::infra::affine::operator-;

namespace {

constexpr std::uint32_t kLogo = 32;
constexpr std::uint32_t kArgbBg = 0xFF101018;
constexpr std::uint32_t kArgbFg = 0xFFE85A30;
constexpr std::uint32_t kArgbAcc = 0xFFFFD36A;

constexpr std::uint64_t pack_row(std::uint32_t y) noexcept {
    std::uint64_t r = 0;
    for (std::uint32_t x = 0; x < kLogo; ++x) {
        const int dx = static_cast<int>(x) - 16;
        const int dy = static_cast<int>(y) - 16;
        const int m = (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
        unsigned p = 0;
        if (m < 14)
            p = ((x ^ y) & 1u) != 0 ? 2u : 1u;
        r |= static_cast<std::uint64_t>(p) << ((31u - x) * 2u);
    }
    return r;
}

constexpr std::array<std::uint64_t, kLogo> kPixmap = {
    pack_row(0),  pack_row(1),  pack_row(2),  pack_row(3),  pack_row(4),  pack_row(5),
    pack_row(6),  pack_row(7),  pack_row(8),  pack_row(9),  pack_row(10), pack_row(11),
    pack_row(12), pack_row(13), pack_row(14), pack_row(15), pack_row(16), pack_row(17),
    pack_row(18), pack_row(19), pack_row(20), pack_row(21), pack_row(22), pack_row(23),
    pack_row(24), pack_row(25), pack_row(26), pack_row(27), pack_row(28), pack_row(29),
    pack_row(30), pack_row(31),
};

}

std::uint32_t TastySplash::pixel(std::uint32_t x, std::uint32_t y) noexcept {
    if (x >= kLogo || y >= kLogo)
        return kArgbBg;
    const std::uint64_t row = kPixmap[y];
    const unsigned p = static_cast<unsigned>((row >> ((31u - x) * 2u)) & 3u);
    if (p == 0)
        return kArgbBg;
    if (p == 2)
        return kArgbAcc;
    return kArgbFg;
}

Ex<void> TastySplash::map(const hal::PhysRegion& fb) noexcept {
    TASTY_SEAT_BODY(TastySplash);
    if (fb_)
        return {};
    fb_region_ = fb;
    auto m = hal::FpgaMemory::map(fb, os::MmioRegion::Access::ReadWrite);
    if (!m)
        return std::unexpected(m.error());
    fb_.emplace(std::move(*m));
    return {};
}

bool TastySplash::blit(app::OutputSize out) noexcept {
    TASTY_SEAT_BODY(TastySplash);
    const app::PictureSize size = app::PictureSize::of(out);
    const app::PixelBytes pixel_bytes = app::PixelBytes::of(sizeof(std::uint32_t));
    if (!fb_ || size.empty())
        return false;
    const app::RowBytes stride = size.stride_bytes(pixel_bytes);
    const std::size_t need = app::kFramebufferPageBytes + app::rows_span(stride, size.rows);
    if (fb_region_.len == 0 || need > fb_region_.len)
        return false;
    auto view = fb_->view(0, need);
    const std::span<std::byte> px = view.subspan(app::kFramebufferPageBytes);
    const auto put =
        [px, stride, pixel_bytes](app::PictureRows y, app::PicturePixels x, std::uint32_t c) {
            const std::span<std::byte> line = px.subspan(app::rows_span(stride, y));
            infra::store_le<std::uint32_t>(
                x.pixel_at(line, pixel_bytes).first<sizeof(std::uint32_t)>(),
                c
            );
        };
    auto* raw = static_cast<unsigned char*>(std::malloc(kSplashEmbedRaw));
    if (raw == nullptr)
        return false;
    uLongf n = kSplashEmbedRaw;
    if (uncompress(raw, &n, kSplashEmbed, kSplashEmbedZ) != Z_OK || n != kSplashEmbedRaw) {
        std::free(raw);
        return false;
    }
    const app::PictureSize source{
        app::PicturePixels::of(kSplashEmbedW),
        app::PictureRows::of(kSplashEmbedH)
    };
    const app::PictureFit fit{source};
    const app::PictureSize drawn = fit.fitted_into(size).size();
    if (drawn.empty()) {
        std::free(raw);
        return false;
    }
    const app::HdRect box = drawn.centre_in(size);
    const app::HdRect frame = out.bounds();
    const app::RowBytes src_stride = source.stride_bytes(pixel_bytes);
    const std::span<const std::byte> rgba = std::as_bytes(
        std::span<const unsigned char>{raw, kSplashEmbedRaw}
    );
    for (std::int32_t y = 0; y < frame.h; ++y) {
        for (std::int32_t x = 0; x < frame.w; ++x) {
            std::uint32_t c = kArgbBg;
            if (x >= box.x && x < box.x + box.w && y >= box.y && y < box.y + box.h) {
                const app::PictureRow sy = source.rows.sample_row(
                    app::PictureRows::of(y - box.y),
                    drawn.rows
                );
                const app::PicturePixels sx = source.width.sample_column(
                    app::PicturePixels::of(x - box.x),
                    drawn.width
                );
                const std::span<const std::byte> line = rgba.subspan(
                    app::rows_span(src_stride, sy)
                );
                const std::span<const std::byte> at = sx.pixel_at(line, pixel_bytes);
                c = 0xFF000000u
                    | (std::to_integer<std::uint32_t>(at[0]) << 16)
                    | (std::to_integer<std::uint32_t>(at[1]) << 8)
                    | std::to_integer<std::uint32_t>(at[2]);
            }
            put(app::PictureRows::of(y), app::PicturePixels::of(x), c);
        }
    }
    std::free(raw);
    return true;
}

Ex<void> TastySplash::show(app::VideoPump& video, app::OutputSize out) noexcept {
    TASTY_SEAT_BODY(TastySplash);
    const app::PictureSize size = app::PictureSize::of(out);
    const std::uint32_t addr = static_cast<std::uint32_t>(
        fb_region_.phys.v + app::kFramebufferPageBytes
    );
    const std::array<std::uint16_t, 10> words{
        kFbEnWord,
        static_cast<std::uint16_t>(addr),
        static_cast<std::uint16_t>(addr >> 16),
        size.width.wire_word(),
        size.rows.wire_word(),
        0,
        (size.width - app::PicturePixels{1}).wire_word(),
        0,
        (size.rows - app::PictureRows{1}).wire_word(),
        size.stride_bytes(app::PixelBytes::of(sizeof(std::uint32_t))).wire_word(),
    };
    return video.wire().publish(proto::UioOp::SetFbuf, words);
}

Ex<void> TastySplash::hide(app::VideoPump& video) noexcept {
    TASTY_SEAT_BODY(TastySplash);
    const std::array<std::uint16_t, 1> words{0};
    return video.wire().publish(proto::UioOp::SetFbuf, words);
}

}
