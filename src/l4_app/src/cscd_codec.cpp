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

Ex<void> CscdCodec::begin(PictureSize size) noexcept {
    TASTY_SEAT_BODY(CscdCodec);
    end();
    if (size.empty() || lzo_init() != LZO_E_OK)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), 0});
    constexpr std::size_t kStripePlanes = 2;
    const std::size_t pic = picture_bytes(size);
    const std::size_t total = up(pic) * kStripePlanes + up(kWorkBytes) + up(dup_cap(pic));
    if (auto r = arena_.reserve(1, total); !r)
        return r;
    const std::span<std::byte> stripe = arena_.stripe(0);
    prev_ = stripe.first(up(pic));
    delta_ = stripe.subspan(up(pic), up(pic));
    work_ = stripe.subspan(kStripePlanes * up(pic), up(kWorkBytes));
    dup_ = stripe.subspan(kStripePlanes * up(pic) + up(kWorkBytes), up(dup_cap(pic)));
    size_ = size;

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
    size_ = PictureSize{};
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
    const PicturePixels width = size_.width;
    if (!open() || !rgb.covers(rgb_row_of(width), size_.rows) || out.size() < payload_bound(size_))
        return {};
    const RowBytes row{row_bytes(width)};
    const RowBytes line = rgb_row_of(width);
    const PictureRows height = size_.rows;
    auto* const prev_bytes = infra::byte_cast<std::uint8_t>(prev_).data();
    auto* const delta_bytes = infra::byte_cast<std::uint8_t>(delta_).data();
    for_each_row(height, [&](PictureRow y) {
        const auto* s = infra::byte_cast<const std::uint8_t>(line.head(rgb.from_row(y))).data();
        const std::size_t at = row_start(row, y.flipped_in(height));
        auto* p = prev_bytes + at;
        auto* d = delta_bytes + at;
        std::size_t px = 0;
        for (PicturePixels x{}; x < width; x += PicturePixels{1}) {
            const std::uint8_t b = s[px + 2], g = s[px + 1], r = s[px];
            d[px] = static_cast<std::uint8_t>(b - p[px]);
            d[px + 1] = static_cast<std::uint8_t>(g - p[px + 1]);
            d[px + 2] = static_cast<std::uint8_t>(r - p[px + 2]);
            p[px] = b;
            p[px + 1] = g;
            p[px + 2] = r;
            px += kRgbPixelBytes;
        }
    });
    const std::size_t picture = picture_bytes(size_);
    return pack_(key ? prev_.first(picture) : delta_.first(picture), key, out);
}

std::span<const std::byte> CscdCodec::rekey(std::span<std::byte> out) noexcept {
    TASTY_SEAT_BODY(CscdCodec);
    if (!open() || out.size() < payload_bound(size_))
        return {};
    return pack_(prev_.first(picture_bytes(size_)), true, out);
}

std::span<const std::byte> CscdCodec::pack_(
    std::span<const std::byte> src,
    bool key,
    std::span<std::byte> out
) noexcept {
    TASTY_SEAT_BODY(CscdCodec);
    const std::size_t picture = picture_bytes(size_);
    if (src.size() != picture || out.size() < payload_bound(size_))
        return {};
    out[0] = key ? std::byte{kKeyBit} : std::byte{0};
    out[1] = std::byte{0};
    lzo_uint n = 0;
    if (lzo1x_1_compress(
            infra::byte_cast<const std::uint8_t>(src).data(),
            src.size(),
            infra::byte_cast<std::uint8_t>(out).data() + kHeadBytes,
            &n,
            work_.data()
        )
        != LZO_E_OK)
        return {};
    return out.first(kHeadBytes + n);
}

}
