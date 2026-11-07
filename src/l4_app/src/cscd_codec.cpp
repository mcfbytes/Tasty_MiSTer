// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/cscd_codec.h"

#include <lzo/lzo1x.h>

#include <cerrno>
#include <cstring>

#include "infra/byte_view.h"

namespace mister::app {

namespace {

constexpr std::size_t kAlign = 64;

constexpr std::size_t up(std::size_t n) noexcept {
    return (n + kAlign - 1) / kAlign * kAlign;
}

constexpr std::size_t dup_cap(std::size_t n) noexcept {
    constexpr std::size_t kDupRatio = 64;
    constexpr std::size_t kDupFloor = 256;
    return n / kDupRatio + kDupFloor;
}

constexpr std::size_t kWorkBytes = LZO1X_1_MEM_COMPRESS;

}

Ex<void> CscdCodec::begin(std::uint16_t w, std::uint16_t h) noexcept {
    TASTY_SEAT_BODY(CscdCodec);
    end();
    if (w == 0 || h == 0 || lzo_init() != LZO_E_OK)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), 0});
    constexpr std::size_t kStripePlanes = 2;
    const std::size_t pic = picture_bytes(w, h);
    const std::size_t total = up(pic) * kStripePlanes + up(kWorkBytes) + up(dup_cap(pic));
    if (auto r = arena_.reserve(1, total); !r)
        return r;
    const std::span<std::byte> stripe = arena_.stripe(0);
    prev_ = stripe.first(up(pic));
    delta_ = stripe.subspan(up(pic), up(pic));
    work_ = stripe.subspan(kStripePlanes * up(pic), up(kWorkBytes));
    dup_ = stripe.subspan(kStripePlanes * up(pic) + up(kWorkBytes), up(dup_cap(pic)));
    width_ = w;
    height_ = h;

    lzo_uint n = 0;
    std::byte* tmp = delta_.data();
    if (lzo1x_1_compress(
            infra::byte_cast<const std::uint8_t>(prev_).data(),
            pic,
            infra::byte_cast<std::uint8_t>(delta_).data(),
            &n,
            work_.data()
        ) != LZO_E_OK
        || kHeadBytes + n > dup_cap(pic)) {
        end();
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), 0});
    }
    dup_[0] = std::byte{0};
    dup_[1] = std::byte{0};
    std::memcpy(dup_.data() + kHeadBytes, tmp, n);
    std::memset(tmp, 0, n);
    dup_len_ = kHeadBytes + n;
    return {};
}

void CscdCodec::end() noexcept {
    TASTY_SEAT_BODY(CscdCodec);
    arena_.release();
    prev_ = delta_ = work_ = dup_ = {};
    dup_len_ = 0;
    width_ = height_ = 0;
}

std::span<const std::byte> CscdCodec::dup() noexcept {
    TASTY_SEAT_BODY(CscdCodec);
    return dup_.first(dup_len_);
}

std::span<const std::byte> CscdCodec::encode(
    RgbRows rgb,
    bool key,
    std::span<std::byte> out
) noexcept {
    TASTY_SEAT_BODY(CscdCodec);
    if (!open()
        || !rgb.covers(rgb_row_of(width_), PictureRows{height_})
        || out.size() < payload_bound(width_, height_))
        return {};
    const RowBytes row{row_bytes(width_)};
    const std::size_t w = width_;
    const RowBytes line = rgb_row_of(w);
    const PictureRows height{height_};
    auto* const prev_bytes = infra::byte_cast<std::uint8_t>(prev_).data();
    auto* const delta_bytes = infra::byte_cast<std::uint8_t>(delta_).data();
    for_each_row(height, [&](PictureRow y) {
        const auto* s = infra::byte_cast<const std::uint8_t>(line.head(rgb.from_row(y))).data();
        const std::size_t at = row_start(row, y.flipped_in(height));
        auto* p = prev_bytes + at;
        auto* d = delta_bytes + at;
        for (std::size_t x = 0; x < w; ++x) {
            const std::size_t px = kRgbPixelBytes * x;
            const std::uint8_t b = s[px + 2], g = s[px + 1], r = s[px];
            d[px] = static_cast<std::uint8_t>(b - p[px]);
            d[px + 1] = static_cast<std::uint8_t>(g - p[px + 1]);
            d[px + 2] = static_cast<std::uint8_t>(r - p[px + 2]);
            p[px] = b;
            p[px + 1] = g;
            p[px + 2] = r;
        }
    });
    return out.first(pack_(key ? prev_.data() : delta_.data(), key, out));
}

std::span<const std::byte> CscdCodec::rekey(std::span<std::byte> out) noexcept {
    TASTY_SEAT_BODY(CscdCodec);
    if (!open() || out.size() < payload_bound(width_, height_))
        return {};
    return out.first(pack_(prev_.data(), true, out));
}

std::size_t CscdCodec::pack_(const std::byte* src, bool key, std::span<std::byte> out) noexcept {
    TASTY_SEAT_BODY(CscdCodec);
    out[0] = key ? std::byte{kKeyBit} : std::byte{0};
    out[1] = std::byte{0};
    lzo_uint n = 0;
    const std::size_t pic = picture_bytes(width_, height_);
    if (lzo1x_1_compress(
            infra::byte_cast<const std::uint8_t>(std::span<const std::byte>(src, pic)).data(),
            pic,
            infra::byte_cast<std::uint8_t>(out).data() + kHeadBytes,
            &n,
            work_.data()
        )
        != LZO_E_OK)
        return 0;
    return kHeadBytes + n;
}

void CscdCodec::half_scale(RgbRows src, std::uint16_t src_w, std::span<std::byte> dst) noexcept {
    constexpr std::size_t kHalf = 2;
    constexpr unsigned kBoxPixels = 4;
    constexpr unsigned kBoxShift = 2;
    constexpr unsigned kBoxRound = kBoxPixels / 2;
    const std::size_t w = src_w;
    const std::size_t ow = w / kHalf;
    const PictureRows oh = src.rows.halved();
    const RowBytes out_row = rgb_row_of(ow);
    auto* const out_bytes = infra::byte_cast<std::uint8_t>(dst.first(rows_span(out_row, oh)))
                                .data();
    const RowBytes row = rgb_row_of(w);
    for_each_row(oh, [&](PictureRow y) {
        const auto* a = infra::byte_cast<const std::uint8_t>(row.head(src.from_row(y.doubled())))
                            .data();
        const auto* b = infra::byte_cast<const std::uint8_t>(
                            src.from_row(y.doubled() + PictureRows{1})
        )
                            .data();
        auto* o = out_bytes + row_start(out_row, y);
        for (std::size_t x = 0; x < ow * kRgbPixelBytes; ++x) {
            const std::size_t c = (x / kRgbPixelBytes) * (kHalf * kRgbPixelBytes)
                + x % kRgbPixelBytes;
            const unsigned sum = kBoxRound
                + a[c]
                + a[c + kRgbPixelBytes]
                + b[c]
                + b[c + kRgbPixelBytes];
            o[x] = static_cast<std::uint8_t>(sum >> kBoxShift);
        }
    });
}

}
