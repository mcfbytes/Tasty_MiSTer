// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/md5.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

#include "infra/endian.h"

namespace mister::cores::mra {
namespace {

constexpr std::array<std::uint32_t, 64> kMdK = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu, 0xf57c0fafu, 0x4787c62au, 0xa8304613u,
    0xfd469501u, 0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu, 0x6b901122u, 0xfd987193u,
    0xa679438eu, 0x49b40821u, 0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau, 0xd62f105du,
    0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u, 0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au, 0xfffa3942u, 0x8771f681u, 0x6d9d6122u,
    0xfde5380cu, 0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u, 0x289b7ec6u, 0xeaa127fau,
    0xd4ef3085u, 0x04881d05u, 0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u, 0xf4292244u,
    0x432aff97u, 0xab9423a7u, 0xfc93a039u, 0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u, 0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu,
    0xeb86d391u,
};

constexpr std::array<unsigned, 64> kMdS = {
    7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 5,  9,  14, 20, 5,  9,
    14, 20, 5,  9,  14, 20, 5,  9,  14, 20, 4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23,
    4,  11, 16, 23, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21,
};

[[nodiscard]] constexpr std::uint32_t rotl(std::uint32_t x, unsigned s) noexcept {
    constexpr unsigned kWordBits = 32;
    return (x << s) | (x >> (kWordBits - s));
}

}

Md5::Md5() noexcept {
    h_ = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u};
}

void Md5::block(const std::uint8_t* p) noexcept {
    constexpr std::size_t kBlockWords = 16;
    constexpr std::size_t kRounds = 64;
    constexpr std::size_t kRoundsPerStage = 16;
    constexpr std::size_t kStage3Begin = 2 * kRoundsPerStage;
    constexpr std::size_t kStage4Begin = 3 * kRoundsPerStage;
    constexpr std::size_t kStage2Mul = 5;
    constexpr std::size_t kStage2Add = 1;
    constexpr std::size_t kStage3Mul = 3;
    constexpr std::size_t kStage3Add = 5;
    constexpr std::size_t kStage4Mul = 7;
    std::array<std::uint32_t, kBlockWords> m{};
    for (std::size_t i = 0; i < kBlockWords; ++i) {
        m[i] = infra::load_le<std::uint32_t>(std::span<const std::uint8_t, sizeof(std::uint32_t)>(
            p + i * sizeof(std::uint32_t),
            sizeof(std::uint32_t)
        ));
    }
    std::uint32_t a = h_[0];
    std::uint32_t b = h_[1];
    std::uint32_t c = h_[2];
    std::uint32_t d = h_[3];
    for (std::size_t i = 0; i < kRounds; ++i) {
        std::uint32_t f = 0;
        std::size_t g = 0;
        if (i < kRoundsPerStage) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < kStage3Begin) {
            f = (d & b) | (~d & c);
            g = (kStage2Mul * i + kStage2Add) % kBlockWords;
        } else if (i < kStage4Begin) {
            f = b ^ c ^ d;
            g = (kStage3Mul * i + kStage3Add) % kBlockWords;
        } else {
            f = c ^ (b | ~d);
            g = (kStage4Mul * i) % kBlockWords;
        }
        const std::uint32_t tmp = d;
        d = c;
        c = b;
        b = b + rotl(a + f + kMdK[i] + m[g], kMdS[i]);
        a = tmp;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
}

void Md5::update(std::span<const std::uint8_t> data) noexcept {
    constexpr std::size_t kBlockBytes = 64;
    std::size_t fill = static_cast<std::size_t>(total_ % kBlockBytes);
    total_ += data.size();
    std::size_t i = 0;
    if (fill != 0) {
        while (fill < kBlockBytes && i < data.size()) {
            tail_[fill++] = data[i++];
        }
        if (fill < kBlockBytes)
            return;
        block(tail_.data());
    }
    for (; i + kBlockBytes <= data.size(); i += kBlockBytes) {
        block(data.data() + i);
    }
    const std::size_t rest = data.size() - i;
    for (std::size_t k = 0; k < rest; ++k) {
        tail_[k] = data[i + k];
    }
}

std::array<std::uint8_t, 16> Md5::digest() noexcept {
    constexpr unsigned kByteBits = 8;
    constexpr std::size_t kBlockBytes = 64;
    constexpr std::size_t kLengthAt = 56;
    constexpr std::size_t kLengthBytes = 8;
    constexpr std::size_t kTwoBlockLengthAt = kBlockBytes + kLengthAt;
    constexpr std::uint8_t kPadMarker = 0x80;
    constexpr std::size_t kStateWords = 4;
    constexpr std::size_t kDigestBytes = 16;
    const std::uint64_t bits = total_ * kByteBits;
    std::array<std::uint8_t, kBlockBytes> pad{};
    pad[0] = kPadMarker;
    const std::size_t fill = static_cast<std::size_t>(total_ % kBlockBytes);
    const std::size_t padlen = (fill < kLengthAt) ? (kLengthAt - fill) : (kTwoBlockLengthAt - fill);
    update({pad.data(), padlen});
    std::array<std::uint8_t, kLengthBytes> lenb{};
    infra::store_le<std::uint64_t>(std::span(lenb), bits);
    update({lenb.data(), lenb.size()});
    std::array<std::uint8_t, kDigestBytes> out{};
    for (std::size_t i = 0; i < kStateWords; ++i) {
        infra::store_le<std::uint32_t>(
            std::span<std::uint8_t, sizeof(std::uint32_t)>(
                out.data() + i * sizeof(std::uint32_t),
                sizeof(std::uint32_t)
            ),
            h_[i]
        );
    }
    return out;
}

Md5Hex Md5::hex_str() noexcept {
    constexpr std::size_t kDigestBytes = 16;
    constexpr unsigned kNibbleBits = 4;
    constexpr unsigned kNibbleMask = 0xF;
    static constexpr char kHex[] = "0123456789abcdef";
    const std::array<std::uint8_t, kDigestBytes> d = digest();
    char out[Md5Hex::kCapacity];
    std::size_t n = 0;
    for (const std::uint8_t b : d) {
        out[n++] = kHex[b >> kNibbleBits];
        out[n++] = kHex[b & kNibbleMask];
    }
    Md5Hex hex;
    (void)hex.assign(std::string_view(out, n));
    return hex;
}

std::string Md5::hex_digest() noexcept {
    return std::string(hex_str().view());
}

}
