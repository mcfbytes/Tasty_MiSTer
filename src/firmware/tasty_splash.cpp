// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_splash.h"

#include <array>
#include <cstdint>
#include <cstdlib>

#include "os/mmio_region.h"

#include "app/video_pump.h"
#include "hal/phys_region.h"
#include "svc/video_service.h"

#ifdef TASTY_SPLASH_EMBED
#include <zlib.h>

#include "splash_embed.h"
#endif

namespace mister::fw {
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
        if (m < 14) p = ((x ^ y) & 1u) != 0 ? 2u : 1u;
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

}  // namespace

std::uint32_t TastySplash::pixel(std::uint32_t x, std::uint32_t y) noexcept {
    if (x >= kLogo || y >= kLogo) return kArgbBg;
    const std::uint64_t row = kPixmap[y];
    const unsigned p = static_cast<unsigned>((row >> ((31u - x) * 2u)) & 3u);
    if (p == 0) return kArgbBg;
    if (p == 2) return kArgbAcc;
    return kArgbFg;
}

Ex<void> TastySplash::map(const hal::PhysRegion& fb) noexcept {
    TASTY_SEAT_BODY(TastySplash);
    if (fb_) return {};
    fb_region_ = fb;
    auto m = hal::FpgaMemory::map(fb, os::MmioRegion::Access::ReadWrite);
    if (!m) return std::unexpected(m.error());
    fb_.emplace(std::move(*m));
    return {};
}

bool TastySplash::blit(std::uint32_t width, std::uint32_t height) noexcept {
    TASTY_SEAT_BODY(TastySplash);
    if (!fb_ || width == 0 || height == 0) return false;
    const std::size_t need = kPixelOff + static_cast<std::size_t>(width) * height * 4u;
    if (fb_region_.len == 0 || need > fb_region_.len) return false;
    auto view = fb_->view(0, need);
    std::uint32_t* dst = reinterpret_cast<std::uint32_t*>(view.data() + kPixelOff);
#ifdef TASTY_SPLASH_EMBED
    auto* raw = static_cast<unsigned char*>(std::malloc(kSplashEmbedRaw));
    if (raw == nullptr) return false;
    uLongf n = kSplashEmbedRaw;
    if (uncompress(raw, &n, kSplashEmbed, kSplashEmbedZ) != Z_OK || n != kSplashEmbedRaw) {
        std::free(raw);
        return false;
    }
    const std::uint32_t iw = kSplashEmbedW;
    const std::uint32_t ih = kSplashEmbedH;
    const std::uint64_t fit_w = static_cast<std::uint64_t>(height) * iw;
    const std::uint64_t fit_h = static_cast<std::uint64_t>(width) * ih;
    std::uint32_t dw = width;
    std::uint32_t dh = height;
    if (fit_w <= fit_h)
        dw = static_cast<std::uint32_t>(fit_w / ih);
    else
        dh = static_cast<std::uint32_t>(fit_h / iw);
    if (dw == 0 || dh == 0) {
        std::free(raw);
        return false;
    }
    const std::uint32_t x0 = (width - dw) / 2;
    const std::uint32_t y0 = (height - dh) / 2;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint32_t c = kArgbBg;
            if (x >= x0 && x < x0 + dw && y >= y0 && y < y0 + dh) {
                const std::uint32_t lx = (x - x0) * iw / dw;
                const std::uint32_t ly = (y - y0) * ih / dh;
                if (lx < iw && ly < ih) {
                    const unsigned char* p = raw + (static_cast<std::size_t>(ly) * iw + lx) * 4u;
                    c = 0xFF000000u | (std::uint32_t{p[0]} << 16) | (std::uint32_t{p[1]} << 8) |
                        std::uint32_t{p[2]};
                }
            }
            dst[y * width + x] = c;
        }
    }
    std::free(raw);
    return true;
#else
    const std::uint32_t side = width < height ? width / 3 : height / 3;
    const std::uint32_t x0 = width / 2 - side / 2;
    const std::uint32_t y0 = height / 2 - side / 2;
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x) {
            std::uint32_t c = kArgbBg;
            if (x >= x0 && x < x0 + side && y >= y0 && y < y0 + side && side != 0) {
                const std::uint32_t lx = (x - x0) * kLogo / side;
                const std::uint32_t ly = (y - y0) * kLogo / side;
                if (lx < kLogo && ly < kLogo) c = pixel(lx, ly);
            }
            dst[y * width + x] = c;
        }
    }
    return true;
#endif
}

Ex<void> TastySplash::show(app::VideoPump& video, std::uint32_t width,
                           std::uint32_t height) noexcept {
    TASTY_SEAT_BODY(TastySplash);
    const std::uint32_t w = width != 0 ? width : kDefaultW;
    const std::uint32_t h = height != 0 ? height : kDefaultH;
    const std::uint32_t addr = static_cast<std::uint32_t>(fb_region_.phys.v + kPixelOff);
    const std::array<std::uint16_t, 10> words{
        kFbEnWord,
        static_cast<std::uint16_t>(addr),
        static_cast<std::uint16_t>(addr >> 16),
        static_cast<std::uint16_t>(w),
        static_cast<std::uint16_t>(h),
        0,
        static_cast<std::uint16_t>(w - 1),
        0,
        static_cast<std::uint16_t>(h - 1),
        static_cast<std::uint16_t>(w * 4),
    };
    return video.wire().publish(svc::kUioSetFbuf, words);
}

Ex<void> TastySplash::hide(app::VideoPump& video) noexcept {
    TASTY_SEAT_BODY(TastySplash);
    const std::array<std::uint16_t, 1> words{0};
    return video.wire().publish(svc::kUioSetFbuf, words);
}

}  // namespace mister::fw
