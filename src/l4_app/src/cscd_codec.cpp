// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/cscd_codec.h"

#include <lzo/lzo1x.h>

#include <cerrno>
#include <cstring>

namespace mister::app {

namespace {

constexpr std::size_t kAlign = 64;

constexpr std::size_t up(std::size_t n) noexcept { return (n + kAlign - 1) / kAlign * kAlign; }

constexpr std::size_t dup_cap(std::size_t n) noexcept { return n / 64u + 256u; }

constexpr std::size_t kWorkBytes = LZO1X_1_MEM_COMPRESS;

}  // namespace

Ex<void> CscdCodec::begin(std::uint16_t w, std::uint16_t h) noexcept {
    TASTY_SEAT_BODY(CscdCodec);
    end();
    if (w == 0 || h == 0 || lzo_init() != LZO_E_OK)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), 0});
    const std::size_t pic = picture_bytes(w, h);
    const std::size_t total = up(pic) * 2 + up(kWorkBytes) + up(dup_cap(pic));
    if (auto r = arena_.reserve(1, total); !r) return r;
    std::byte* p = arena_.stripe(0).data();
    prev_ = p;
    delta_ = prev_ + up(pic);
    work_ = delta_ + up(pic);
    dup_ = work_ + up(kWorkBytes);
    width_ = w;
    height_ = h;

    lzo_uint n = 0;
    std::byte* tmp = delta_;
    if (lzo1x_1_compress(reinterpret_cast<const unsigned char*>(prev_), pic,
                         reinterpret_cast<unsigned char*>(tmp), &n, work_) != LZO_E_OK ||
        kHeadBytes + n > dup_cap(pic)) {
        end();
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), 0});
    }
    dup_[0] = std::byte{0};
    dup_[1] = std::byte{0};
    std::memcpy(dup_ + kHeadBytes, tmp, n);
    std::memset(tmp, 0, n);
    dup_len_ = kHeadBytes + n;
    return {};
}

void CscdCodec::end() noexcept {
    TASTY_SEAT_BODY(CscdCodec);
    arena_.release();
    prev_ = delta_ = work_ = dup_ = nullptr;
    dup_len_ = 0;
    width_ = height_ = 0;
}

std::span<const std::byte> CscdCodec::dup() noexcept {
    TASTY_SEAT_BODY(CscdCodec);
    return {dup_, dup_len_};
}

std::size_t CscdCodec::encode(const std::byte* rgb, std::size_t line, bool key,
                              std::span<std::byte> out) noexcept {
    TASTY_SEAT_BODY(CscdCodec);
    if (!open() || rgb == nullptr || out.size() < payload_bound(width_, height_)) return 0;
    const std::size_t row = row_bytes(width_);
    const std::size_t w = width_;
    for (std::size_t y = 0; y < height_; ++y) {
        const auto* s = reinterpret_cast<const std::uint8_t*>(rgb + y * line);
        const std::size_t at = (height_ - 1u - y) * row;
        auto* p = reinterpret_cast<std::uint8_t*>(prev_ + at);
        auto* d = reinterpret_cast<std::uint8_t*>(delta_ + at);
        for (std::size_t x = 0; x < w; ++x) {
            const std::uint8_t b = s[3 * x + 2], g = s[3 * x + 1], r = s[3 * x];
            d[3 * x] = static_cast<std::uint8_t>(b - p[3 * x]);
            d[3 * x + 1] = static_cast<std::uint8_t>(g - p[3 * x + 1]);
            d[3 * x + 2] = static_cast<std::uint8_t>(r - p[3 * x + 2]);
            p[3 * x] = b;
            p[3 * x + 1] = g;
            p[3 * x + 2] = r;
        }
    }
    return pack_(key ? prev_ : delta_, key, out);
}

std::size_t CscdCodec::rekey(std::span<std::byte> out) noexcept {
    TASTY_SEAT_BODY(CscdCodec);
    if (!open() || out.size() < payload_bound(width_, height_)) return 0;
    return pack_(prev_, true, out);
}

std::size_t CscdCodec::pack_(const std::byte* src, bool key, std::span<std::byte> out) noexcept {
    TASTY_SEAT_BODY(CscdCodec);
    out[0] = key ? std::byte{kKeyBit} : std::byte{0};
    out[1] = std::byte{0};
    lzo_uint n = 0;
    if (lzo1x_1_compress(
            reinterpret_cast<const unsigned char*>(src), picture_bytes(width_, height_),
            reinterpret_cast<unsigned char*>(out.data() + kHeadBytes), &n, work_) != LZO_E_OK)
        return 0;
    return kHeadBytes + n;
}

void CscdCodec::half_scale(const std::byte* rgb, std::size_t line, std::uint16_t w, std::uint16_t h,
                           std::byte* dst) noexcept {
    const std::size_t ow = w / 2u;
    const std::size_t oh = h / 2u;
    for (std::size_t y = 0; y < oh; ++y) {
        const auto* a = reinterpret_cast<const std::uint8_t*>(rgb + 2 * y * line);
        const auto* b = a + line;
        auto* o = reinterpret_cast<std::uint8_t*>(dst + y * ow * 3u);
        for (std::size_t x = 0; x < ow * 3u; ++x) {
            const std::size_t c = (x / 3u) * 6u + x % 3u;
            const unsigned sum = 2u + a[c] + a[c + 3] + b[c] + b[c + 3];
            o[x] = static_cast<std::uint8_t>(sum >> 2);
        }
    }
}

}  // namespace mister::app
