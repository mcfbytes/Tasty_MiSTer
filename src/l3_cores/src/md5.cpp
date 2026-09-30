// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/md5.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>

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

constexpr std::array<int, 64> kMdS = {
    7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 7,  12, 17, 22, 5,  9,  14, 20, 5,  9,
    14, 20, 5,  9,  14, 20, 5,  9,  14, 20, 4,  11, 16, 23, 4,  11, 16, 23, 4,  11, 16, 23,
    4,  11, 16, 23, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21, 6,  10, 15, 21,
};

[[nodiscard]] constexpr std::uint32_t rotl(std::uint32_t x, int s) noexcept {
    return (x << s) | (x >> (32 - s));
}

}  // namespace

Md5::Md5() noexcept { h_ = {0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u}; }

void Md5::block(const std::uint8_t* p) noexcept {
    std::array<std::uint32_t, 16> m{};
    for (std::size_t i = 0; i < 16; ++i) {
        m[i] = static_cast<std::uint32_t>(p[i * 4]) |
               (static_cast<std::uint32_t>(p[i * 4 + 1]) << 8) |
               (static_cast<std::uint32_t>(p[i * 4 + 2]) << 16) |
               (static_cast<std::uint32_t>(p[i * 4 + 3]) << 24);
    }
    std::uint32_t a = h_[0];
    std::uint32_t b = h_[1];
    std::uint32_t c = h_[2];
    std::uint32_t d = h_[3];
    for (std::size_t i = 0; i < 64; ++i) {
        std::uint32_t f = 0;
        std::size_t g = 0;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
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
    std::size_t fill = static_cast<std::size_t>(total_ % 64);
    total_ += data.size();
    std::size_t i = 0;
    if (fill != 0) {
        while (fill < 64 && i < data.size()) {
            tail_[fill++] = data[i++];
        }
        if (fill < 64) return;
        block(tail_.data());
    }
    for (; i + 64 <= data.size(); i += 64) {
        block(data.data() + i);
    }
    const std::size_t rest = data.size() - i;
    for (std::size_t k = 0; k < rest; ++k) {
        tail_[k] = data[i + k];
    }
}

std::array<std::uint8_t, 16> Md5::digest() noexcept {
    const std::uint64_t bits = total_ * 8;
    std::array<std::uint8_t, 64> pad{};
    pad[0] = 0x80;
    const std::size_t fill = static_cast<std::size_t>(total_ % 64);
    const std::size_t padlen = (fill < 56) ? (56 - fill) : (120 - fill);
    update({pad.data(), padlen});
    std::array<std::uint8_t, 8> lenb{};
    for (std::size_t i = 0; i < 8; ++i) {
        lenb[i] = static_cast<std::uint8_t>(bits >> (8 * i));
    }
    update({lenb.data(), 8});
    std::array<std::uint8_t, 16> out{};
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = 0; j < 4; ++j) {
            out[i * 4 + j] = static_cast<std::uint8_t>(h_[i] >> (8 * j));
        }
    }
    return out;
}

Md5Hex Md5::hex_str() noexcept {
    static constexpr char kHex[] = "0123456789abcdef";
    const std::array<std::uint8_t, 16> d = digest();
    char out[Md5Hex::kCapacity];
    std::size_t n = 0;
    for (const std::uint8_t b : d) {
        out[n++] = kHex[b >> 4];
        out[n++] = kHex[b & 0xF];
    }
    Md5Hex hex;
    (void)hex.assign(std::string_view(out, n));
    return hex;
}

std::string Md5::hex_digest() noexcept { return std::string(hex_str().view()); }

}  // namespace mister::cores::mra
