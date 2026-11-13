// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/png_encoder.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "svc/crc32_accumulator.h"
#include "svc/types.h"

namespace mister::app {

namespace {

struct PngIhdr {

    std::array<std::uint8_t, sizeof(std::uint32_t) * 2 + 5> bytes{};

    [[nodiscard]] static constexpr PngIhdr of(PictureSize size) noexcept {
        constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
        constexpr std::size_t kWidthAt = 0;
        constexpr std::size_t kHeightAt = kWordBytes;
        constexpr std::size_t kTailAt = kHeightAt + kWordBytes;
        constexpr std::array<std::uint8_t, 5> kTail{8, 2, 0, 0, 0};
        PngIhdr ihdr{};
        const std::span<std::uint8_t> bytes{ihdr.bytes};
        size.pixels().store_be32(bytes.subspan<kWidthAt, kWordBytes>());
        size.lines().store_be32(bytes.subspan<kHeightAt, kWordBytes>());
        for (std::size_t i = 0; i < kTail.size(); ++i)
            bytes[kTailAt + i] = kTail[i];
        return ihdr;
    }
};

}

std::vector<std::uint8_t> PngEncoder::encode(
    PictureSize size,
    std::span<const std::uint8_t> rgb
) const {
    static_assert(std::endian::native == std::endian::little);
    constexpr std::size_t kChannels = 3;
    constexpr std::size_t kStoredBlockMax = 65535;
    constexpr std::size_t kStoredHeaderBytes = 5;
    constexpr std::size_t kZlibHeaderBytes = 2;
    constexpr std::size_t kAdlerBytes = 4;
    constexpr std::uint32_t kAdlerMod = 65521;
    constexpr std::uint32_t kIendCrc = 0xAE426082u;
    constexpr std::uint32_t kIhdrLength = 13;
    constexpr std::array<std::uint8_t, 8> kSignature{0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

    const RowBytes stride = size.stride_bytes(PixelBytes::of(kChannels));
    const std::size_t h = rows_span(RowBytes{1}, size.rows);
    if (size.empty() || rgb.size() != rows_span(stride, size.rows))
        return {};
    std::vector<std::uint8_t> out;
    const std::size_t row_pixels = rgb.size() / h;
    const std::size_t raw_len = rgb.size() + h;

    auto be32 = [&out](std::uint32_t v) {
        const auto le = std::bit_cast<std::array<std::uint8_t, 4>>(v);
        out.insert(out.end(), le.rbegin(), le.rend());
    };

    auto be32_crc = [&out](std::span<const std::uint8_t> chunk) {
        svc::Crc32AccumulatorT<seat_owner::Diag> crc;
        crc.extend(chunk);
        const auto be = crc.value().to_be_bytes();
        out.insert(out.end(), be.begin(), be.end());
    };

    out.insert(out.end(), kSignature.begin(), kSignature.end());

    be32(kIhdrLength);
    const std::size_t ihdr_at = out.size();
    out.insert(out.end(), {'I', 'H', 'D', 'R'});
    const PngIhdr ihdr = PngIhdr::of(size);
    out.insert(out.end(), ihdr.bytes.begin(), ihdr.bytes.end());
    be32_crc(std::span<const std::uint8_t>(out).subspan(ihdr_at));

    const std::size_t blocks = (raw_len + kStoredBlockMax - 1) / kStoredBlockMax;
    be32(
        static_cast<std::uint32_t>(
            kZlibHeaderBytes + raw_len + blocks * kStoredHeaderBytes + kAdlerBytes
        )
    );
    const std::size_t idat_at = out.size();
    out.insert(out.end(), {'I', 'D', 'A', 'T'});
    out.push_back(0x78);
    out.push_back(0x01);

    std::uint32_t a = 1, b = 0;
    std::size_t emitted = 0;
    std::size_t block_left = 0;
    auto raw_byte = [&](std::uint8_t v) {
        if (block_left == 0) {
            const std::size_t left = raw_len - emitted;
            block_left = left < kStoredBlockMax ? left : kStoredBlockMax;
            out.push_back(left <= kStoredBlockMax ? 1 : 0);
            const auto len = static_cast<std::uint16_t>(block_left);
            const auto nlen = static_cast<std::uint16_t>(~len);
            const auto len_le = std::bit_cast<std::array<std::uint8_t, 2>>(len);
            const auto nlen_le = std::bit_cast<std::array<std::uint8_t, 2>>(nlen);
            out.insert(out.end(), len_le.begin(), len_le.end());
            out.insert(out.end(), nlen_le.begin(), nlen_le.end());
        }
        out.push_back(v);
        a = (a + v) % kAdlerMod;
        b = (b + a) % kAdlerMod;
        ++emitted;
        --block_left;
    };
    for (std::size_t y = 0; y < h; ++y) {
        raw_byte(0);
        const std::size_t base = y * row_pixels;
        for (std::size_t i = 0; i < row_pixels; ++i)
            raw_byte(rgb[base + i]);
    }
    be32((b << std::numeric_limits<std::uint16_t>::digits) | a);
    be32_crc(std::span<const std::uint8_t>(out).subspan(idat_at));

    be32(0);
    out.insert(out.end(), {'I', 'E', 'N', 'D'});
    be32(kIendCrc);
    return out;
}

}
