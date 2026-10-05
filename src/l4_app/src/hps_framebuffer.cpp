// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hps_framebuffer.h"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cstdio>

#include "app/video_pump.h"
#include "svc/video_service.h"

namespace mister::app {
namespace {

constexpr std::uint16_t kFmt565 = 0b00100;
constexpr std::uint16_t kFmt1555 = 0b01100;
constexpr std::uint16_t kFmt8888 = 0b00110;
constexpr std::uint16_t kFmtPal8 = 0b00011;
constexpr std::uint16_t kFmtRxB = 0b10000;
constexpr std::uint16_t kFbEn = 0x8000;

constexpr std::uint32_t kFbPixels = 1920u * 1080u;
constexpr std::uint32_t kPixelOff = 4096;

}  // namespace

Ex<void> HpsFramebuffer::publish_(std::uint16_t fmt, std::uint32_t w, std::uint32_t h,
                                  std::uint32_t hmin, std::uint32_t hmax, std::uint32_t vmin,
                                  std::uint32_t vmax, std::uint32_t stride) noexcept {
    const auto addr = static_cast<std::uint32_t>(fb_.phys.v + kPixelOff);
    const std::array<std::uint16_t, 10> words{
        static_cast<std::uint16_t>(kFbEn | fmt), static_cast<std::uint16_t>(addr),
        static_cast<std::uint16_t>(addr >> 16),  static_cast<std::uint16_t>(w),
        static_cast<std::uint16_t>(h),           static_cast<std::uint16_t>(hmin),
        static_cast<std::uint16_t>(hmax),        static_cast<std::uint16_t>(vmin),
        static_cast<std::uint16_t>(vmax),        static_cast<std::uint16_t>(stride),
    };
    return video_.wire().publish(svc::kUioSetFbuf, words);
}

void HpsFramebuffer::write_mode_param_(unsigned fmt, unsigned rb, std::uint32_t w, std::uint32_t h,
                                       std::uint32_t stride) noexcept {
    const int fd = ::open(mode_param_.c_str(), O_WRONLY | O_CLOEXEC | O_TRUNC);
    if (fd < 0) return;
    char line[64];
    const int n = std::snprintf(line, sizeof line, "%u %u %u %u %u\n", fmt, rb, w, h, stride);
    if (n > 0 && ::write(fd, line, static_cast<std::size_t>(n)) < 0) ++refused_;
    (void)::close(fd);
}

Ex<void> HpsFramebuffer::raise() noexcept {
    TASTY_SEAT_BODY(HpsFramebuffer);
    const std::uint32_t ow = video_.output_width();
    const std::uint32_t oh = video_.output_height();
    if (fb_.len == 0 || ow == 0 || oh == 0)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    const std::uint32_t scale = ow * oh > kFbPixels ? 2u : 1u;
    width_ = ow / scale;
    height_ = oh / scale;
    raised_ = true;
    write_mode_param_(8888, 1, width_, height_, width_ * 4u);
    if (held_) return {};
    return publish_(kFmtRxB | kFmt8888, width_, height_, 0, ow - 1, 0, oh - 1, width_ * 4u);
}

Ex<void> HpsFramebuffer::drop() noexcept {
    TASTY_SEAT_BODY(HpsFramebuffer);
    raised_ = false;
    if (held_) return {};
    const std::array<std::uint16_t, 1> off{0};
    return video_.wire().publish(svc::kUioSetFbuf, off);
}

bool HpsFramebuffer::take_fb_cmd(std::string_view line) noexcept {
    TASTY_SEAT_BODY(HpsFramebuffer);
    ++fb_cmds_;
    if (!raised_ || held_) {
        ++refused_;
        return true;
    }
    const std::uint32_t ow = video_.output_width();
    const std::uint32_t oh = video_.output_height();
    const std::string text(line);
    int fmt = 0;
    int rb = 0;
    int div = -1;
    int w = -1;
    int h = -1;
    std::uint32_t hmin = 0;
    std::uint32_t hmax = ow - 1;
    std::uint32_t vmin = 0;
    std::uint32_t vmax = oh - 1;
    bool accept = false;
    bool write_param = true;
    if (std::sscanf(text.c_str(), "fb_cmd0 %d %d %d", &fmt, &rb, &div) == 3 ||
        std::sscanf(text.c_str(), "fb_cmd2 %d %d %d", &fmt, &rb, &div) == 3) {
        if (div >= 1 && div <= 4) {
            w = static_cast<int>(ow) / div;
            h = static_cast<int>(oh) / div;
            accept = true;
            write_param = text.size() > 6 && text[6] != '2';
        }
    } else if (std::sscanf(text.c_str(), "fb_cmd1 %d %d %d %d", &fmt, &rb, &w, &h) == 4) {
        if (w < 120 || w > static_cast<int>(ow)) w = static_cast<int>(ow);
        if (h < 120 || h > static_cast<int>(oh)) h = static_cast<int>(oh);

        int d = 1;
        while (w * (d + 1) <= static_cast<int>(ow) && h * (d + 1) <= static_cast<int>(oh))
            ++d;
        hmin = (ow - static_cast<std::uint32_t>(w * d)) / 2u;
        vmin = (oh - static_cast<std::uint32_t>(h * d)) / 2u;
        hmax = hmin + static_cast<std::uint32_t>(w * d) - 1u;
        vmax = vmin + static_cast<std::uint32_t>(h * d) - 1u;
        accept = true;
    }
    std::uint16_t sc = 0;
    unsigned bpp = 0;
    if (accept) {
        switch (fmt) {
            case 8888:
                bpp = 4;
                sc = kFmt8888;
                break;
            case 1555:
                bpp = 2;
                sc = kFmt1555;
                break;
            case 565:
                bpp = 2;
                sc = kFmt565;
                break;
            case 8:
                bpp = 1;
                sc = kFmtPal8;
                rb = 0;
                break;
            default:
                accept = false;
        }
    }
    if (!accept) {
        ++refused_;
        return true;
    }
    if (rb != 0) {
        sc |= kFmtRxB;
        rb = 1;
    }
    const auto uw = static_cast<std::uint32_t>(w);
    const auto uh = static_cast<std::uint32_t>(h);
    const std::uint32_t stride = ((uw * bpp) + 15u) & ~15u;
    width_ = uw;
    height_ = uh;
    if (!publish_(sc, uw, uh, hmin, hmax, vmin, vmax, stride)) ++refused_;
    if (write_param)
        write_mode_param_(static_cast<unsigned>(fmt), static_cast<unsigned>(rb), uw, uh, stride);
    return true;
}

}  // namespace mister::app
