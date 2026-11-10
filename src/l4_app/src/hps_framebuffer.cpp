// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/hps_framebuffer.h"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cstdio>
#include <optional>
#include <string>

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

constexpr PictureSize kFbFrame{PicturePixels::of(1920), PictureRows::of(1080)};

}

FbScanWindow FbScanWindow::centred(const FbView& picture, PictureSize out) noexcept {
    return FbScanWindow{picture.size.centre_in(out)};
}

Ex<void> HpsFramebuffer::publish_(const FbView& picture, FbScanWindow window) noexcept {
    const auto [addr_lo, addr_hi] = fb_.phys.advanced(picture.offset).low_halves();
    std::uint16_t sc = 0;
    switch (picture.format) {
        case FbView::FbFormat::Argb8888:
            sc = kFmt8888;
            break;
        case FbView::FbFormat::Argb1555:
            sc = kFmt1555;
            break;
        case FbView::FbFormat::Rgb565:
            sc = kFmt565;
            break;
        case FbView::FbFormat::Pal8:
            sc = kFmtPal8;
            break;
    }
    if (picture.red_blue_swap)
        sc |= kFmtRxB;
    const auto [hmin, hmax, vmin, vmax] = fb_scan_words(window);
    const std::array<std::uint16_t, 10> words{
        static_cast<std::uint16_t>(kFbEn | sc),
        addr_lo,
        addr_hi,
        picture.size.pixels().wire_word(),
        picture.size.lines().wire_word(),
        hmin,
        hmax,
        vmin,
        vmax,
        picture.stride.wire_word(),
    };
    return wire_.publish(proto::UioOp::SetFbuf, words);
}

void HpsFramebuffer::write_mode_param_(const FbView& picture) noexcept {
    const std::string line = picture.mode_line();
    const int fd = ::open(mode_param_.c_str(), O_WRONLY | O_CLOEXEC | O_TRUNC);
    if (fd < 0)
        return;
    if (::write(fd, line.data(), line.size()) < 0)
        ++refused_;
    (void)::close(fd);
}

Ex<void> HpsFramebuffer::raise() noexcept {
    TASTY_SEAT_BODY(HpsFramebuffer);
    constexpr std::size_t kBytes8888 = 4;
    const PictureSize out = output_size_();
    if (fb_.len == 0 || out.width.empty() || out.rows.empty())
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    const PictureSize picture_size = out.halved_if(!out.fits_scaler_frame(kFbFrame));
    picture_ = FbView::of(
        picture_size,
        picture_size.stride_bytes(PixelBytes::of(kBytes8888)),
        FbView::FbFormat::Argb8888,
        true
    );
    raised_ = true;
    viewing_ = false;
    write_mode_param_(picture_);
    if (held_)
        return {};
    return publish_(picture_, FbScanWindow::whole(out));
}

Ex<void> HpsFramebuffer::raise_view(const FbView& view) noexcept {
    TASTY_SEAT_BODY(HpsFramebuffer);
    const PictureSize out = output_size_();
    if (fb_.len == 0 || out.width.empty() || out.rows.empty())
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    const std::uint64_t bytes = rows_span(view.stride, view.size.lines());
    if (view.offset > fb_.len || bytes > fb_.len - view.offset)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), view.offset});
    picture_ = view;

    if (picture_.format != FbView::FbFormat::Rgb565)
        picture_.red_blue_swap = true;
    raised_ = true;
    viewing_ = true;
    if (held_)
        return {};
    return publish_(picture_, FbScanWindow::whole(out));
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
    constexpr std::size_t kBytes8888 = 4, kBytes16 = 2, kBytes8 = 1;
    constexpr std::size_t kStrideAlign = 16;
    if (!raised_ || held_ || viewing_) {
        ++refused_;
        return true;
    }
    const PictureSize out = output_size_();
    const std::string text(line);
    int fmt = 0;
    int rb = 0;
    int div = -1;
    int w = -1;
    int h = -1;
    PictureSize size{};
    bool accept = false;
    bool centred = false;
    bool write_param = true;
    if (std::sscanf(text.c_str(), "fb_cmd0 %d %d %d", &fmt, &rb, &div) == kDivisorFields
        || std::sscanf(text.c_str(), "fb_cmd2 %d %d %d", &fmt, &rb, &div) == kDivisorFields) {
        if (div >= kMinDivisor && div <= kMaxDivisor) {
            size = PictureSize{out.pixels().divided_by(div), out.lines().divided_by(div)};
            accept = true;
            write_param = text.size() > kCmdDigitAt && text[kCmdDigitAt] != kCmd2;
        }
    } else if (
        std::sscanf(text.c_str(), "fb_cmd1 %d %d %d %d", &fmt, &rb, &w, &h) == kGeometryFields
    ) {

        w = out.pixels().clamp_side(w, kMinSide);
        h = out.lines().clamp_side(h, kMinSide);
        const PictureSize wanted{PicturePixels::of(w), PictureRows::of(h)};
        size = out.clamped_multiple_of(wanted, PicturePixels::of(kMinSide));
        accept = true;
        centred = true;
    }
    std::optional<FbView::FbFormat> format;
    switch (fmt) {
        case kName8888:
            format = FbView::FbFormat::Argb8888;
            break;
        case kName1555:
            format = FbView::FbFormat::Argb1555;
            break;
        case kName565:
            format = FbView::FbFormat::Rgb565;
            break;
        case kNamePal8:
            format = FbView::FbFormat::Pal8;
            break;
        default:
            break;
    }
    if (!accept || !format) {
        ++refused_;
        return true;
    }
    std::size_t bpp = 0;
    switch (*format) {
        case FbView::FbFormat::Argb8888:
            bpp = kBytes8888;
            break;
        case FbView::FbFormat::Argb1555:
            bpp = kBytes16;
            break;
        case FbView::FbFormat::Rgb565:
            bpp = kBytes16;
            break;
        case FbView::FbFormat::Pal8:
            bpp = kBytes8;
            break;
    }
    const RowBytes stride = size.stride_bytes(PixelBytes::of(bpp), RowAlign::of(kStrideAlign));
    const FbView
        picture = FbView::of(size, stride, *format, *format != FbView::FbFormat::Pal8 && rb != 0);
    picture_ = picture;
    const FbScanWindow window = centred ? FbScanWindow::centred(picture, out)
                                        : FbScanWindow::whole(out);
    if (!publish_(picture, window))
        ++refused_;
    if (write_param)
        write_mode_param_(picture);
    return true;
}

}
