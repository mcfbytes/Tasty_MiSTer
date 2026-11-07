// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hps_framebuffer.h"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cstdio>

#include "os/types.h"
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

}

Ex<void> HpsFramebuffer::publish_(
    os::PhysAddr pixels,
    std::uint16_t fmt,
    std::uint32_t w,
    std::uint32_t h,
    std::uint32_t hmin,
    std::uint32_t hmax,
    std::uint32_t vmin,
    std::uint32_t vmax,
    RowBytes stride
) noexcept {
    const auto [addr_lo, addr_hi] = pixels.low_halves();
    const std::array<std::uint16_t, 10> words{
        static_cast<std::uint16_t>(kFbEn | fmt),
        addr_lo,
        addr_hi,
        static_cast<std::uint16_t>(w),
        static_cast<std::uint16_t>(h),
        static_cast<std::uint16_t>(hmin),
        static_cast<std::uint16_t>(hmax),
        static_cast<std::uint16_t>(vmin),
        static_cast<std::uint16_t>(vmax),
        static_cast<std::uint16_t>(stride.v),
    };
    return wire_.publish(proto::UioOp::SetFbuf, words);
}

void HpsFramebuffer::write_mode_param_(
    unsigned fmt,
    unsigned rb,
    std::uint32_t w,
    std::uint32_t h,
    RowBytes stride
) noexcept {
    const int fd = ::open(mode_param_.c_str(), O_WRONLY | O_CLOEXEC | O_TRUNC);
    if (fd < 0)
        return;
    char line[64];
    const int n = std::snprintf(
        line,
        sizeof line,
        "%u %u %u %u %s\n",
        fmt,
        rb,
        w,
        h,
        stride.text().c_str()
    );
    if (n > 0 && ::write(fd, line, static_cast<std::size_t>(n)) < 0)
        ++refused_;
    (void)::close(fd);
}

Ex<void> HpsFramebuffer::raise() noexcept {
    TASTY_SEAT_BODY(HpsFramebuffer);
    constexpr std::uint32_t kBigOutputScale = 2, kBytes8888 = 4;
    constexpr int kName8888 = 8888;
    const std::uint32_t ow = output_.output_width();
    const std::uint32_t oh = output_.output_height();
    if (fb_.len == 0 || ow == 0 || oh == 0)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    const std::uint32_t scale = ow * oh > kFbPixels ? kBigOutputScale : 1u;
    width_ = ow / scale;
    height_ = oh / scale;
    raised_ = true;
    viewing_ = false;
    write_mode_param_(kName8888, 1, width_, height_, RowBytes{width_ * kBytes8888});
    if (held_)
        return {};
    return publish_(
        fb_.phys.advanced(kFramebufferPageBytes),
        static_cast<std::uint16_t>(kFmtRxB | kFmt8888),
        width_,
        height_,
        0,
        ow - 1,
        0,
        oh - 1,
        RowBytes{std::size_t{width_} * kBytes8888}
    );
}

Ex<void> HpsFramebuffer::raise_view(const FbView& view) noexcept {
    TASTY_SEAT_BODY(HpsFramebuffer);
    const std::uint32_t ow = output_.output_width();
    const std::uint32_t oh = output_.output_height();
    if (fb_.len == 0 || ow == 0 || oh == 0)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    const std::uint64_t bytes = rows_span(view.stride, view.height);
    if (view.offset > fb_.len || bytes > fb_.len - view.offset)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), view.offset});
    const std::uint16_t fmt = view.format == FbView::FbFormat::Rgb565
        ? kFmt565
        : static_cast<std::uint16_t>(kFmtRxB | kFmt8888);
    width_ = view.width;
    height_ = view.height;
    raised_ = true;
    viewing_ = true;
    if (held_)
        return {};
    return publish_(
        fb_.phys.advanced(view.offset),
        fmt,
        view.width,
        view.height,
        0,
        ow - 1,
        0,
        oh - 1,
        view.stride
    );
}

Ex<void> HpsFramebuffer::drop() noexcept {
    TASTY_SEAT_BODY(HpsFramebuffer);
    raised_ = false;
    viewing_ = false;
    if (held_)
        return {};
    const std::array<std::uint16_t, 1> off{0};
    return wire_.publish(proto::UioOp::SetFbuf, off);
}

bool HpsFramebuffer::take_fb_cmd(std::string_view line) noexcept {
    TASTY_SEAT_BODY(HpsFramebuffer);
    constexpr int kName8888 = 8888, kName1555 = 1555, kName565 = 565, kNamePal8 = 8;

    constexpr int kMinDivisor = 1, kMaxDivisor = 4, kMinSide = 120;

    constexpr int kDivisorFields = 3, kGeometryFields = 4;

    constexpr std::size_t kCmdDigitAt = 6;
    constexpr char kCmd2 = '2';
    constexpr unsigned kBpp8888 = 4, kBpp16 = 2, kBpp8 = 1;
    constexpr std::uint32_t kCentre = 2, kStrideAlignMask = 16 - 1u;
    ++fb_cmds_;
    if (!raised_ || held_ || viewing_) {
        ++refused_;
        return true;
    }
    const std::uint32_t ow = output_.output_width();
    const std::uint32_t oh = output_.output_height();
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
    if (std::sscanf(text.c_str(), "fb_cmd0 %d %d %d", &fmt, &rb, &div) == kDivisorFields
        || std::sscanf(text.c_str(), "fb_cmd2 %d %d %d", &fmt, &rb, &div) == kDivisorFields) {
        if (div >= kMinDivisor && div <= kMaxDivisor) {
            w = static_cast<int>(ow) / div;
            h = static_cast<int>(oh) / div;
            accept = true;
            write_param = text.size() > kCmdDigitAt && text[kCmdDigitAt] != kCmd2;
        }
    } else if (
        std::sscanf(text.c_str(), "fb_cmd1 %d %d %d %d", &fmt, &rb, &w, &h) == kGeometryFields
    ) {
        if (w < kMinSide || w > static_cast<int>(ow))
            w = static_cast<int>(ow);
        if (h < kMinSide || h > static_cast<int>(oh))
            h = static_cast<int>(oh);

        int d = 1;
        while (w * (d + 1) <= static_cast<int>(ow) && h * (d + 1) <= static_cast<int>(oh))
            ++d;
        hmin = (ow - static_cast<std::uint32_t>(w * d)) / kCentre;
        vmin = (oh - static_cast<std::uint32_t>(h * d)) / kCentre;
        hmax = hmin + static_cast<std::uint32_t>(w * d) - 1u;
        vmax = vmin + static_cast<std::uint32_t>(h * d) - 1u;
        accept = true;
    }
    std::uint16_t sc = 0;
    unsigned bpp = 0;
    if (accept) {
        switch (fmt) {
            case kName8888:
                bpp = kBpp8888;
                sc = kFmt8888;
                break;
            case kName1555:
                bpp = kBpp16;
                sc = kFmt1555;
                break;
            case kName565:
                bpp = kBpp16;
                sc = kFmt565;
                break;
            case kNamePal8:
                bpp = kBpp8;
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
    const RowBytes stride{(uw * bpp + kStrideAlignMask) & ~kStrideAlignMask};
    width_ = uw;
    height_ = uh;
    if (!publish_(
            fb_.phys.advanced(kFramebufferPageBytes),
            sc,
            uw,
            uh,
            hmin,
            hmax,
            vmin,
            vmax,
            stride
        ))
        ++refused_;
    if (write_param)
        write_mode_param_(static_cast<unsigned>(fmt), static_cast<unsigned>(rb), uw, uh, stride);
    return true;
}

}
