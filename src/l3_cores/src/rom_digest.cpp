// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/rom_digest.h"

#include <algorithm>
#include <bit>
#include <cstddef>

#include "infra/crc32.h"

namespace mister::cores {
namespace {

constexpr std::array<std::uint32_t, 5> kSha1Init{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u,
                                                 0xC3D2E1F0u};
constexpr std::array<std::uint32_t, 8> kSha256Init{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u,
                                                   0xa54ff53au, 0x510e527fu, 0x9b05688cu,
                                                   0x1f83d9abu, 0x5be0cd19u};
constexpr std::array<std::uint32_t, 64> kSha256K{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) |
           std::uint32_t{p[3]};
}

int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

std::optional<DigestValue> parse_hex_digest(std::string_view text, DigestKind kind) noexcept {
    DigestValue v{.kind = kind, .len = digest_len(kind)};
    if (text.size() != 2u * v.len) return std::nullopt;
    for (std::size_t i = 0; i < v.len; ++i) {
        const int hi = hex(text[2 * i]);
        const int lo = hex(text[2 * i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        v.bytes[i] = static_cast<std::uint8_t>(hi << 4 | lo);
    }
    return v;
}

RomDigest::RomDigest(DigestKind kind) noexcept : kind_(kind) {
    if (kind_ == DigestKind::Sha1) std::copy(kSha1Init.begin(), kSha1Init.end(), h_.begin());
    if (kind_ == DigestKind::Sha256) h_ = kSha256Init;
}

void RomDigest::sha1_block_(const std::uint8_t* p) noexcept {
    std::array<std::uint32_t, 80> w{};
    for (std::size_t i = 0; i < 16; ++i)
        w[i] = be32(p + 4 * i);
    for (std::size_t i = 16; i < 80; ++i)
        w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
    for (std::size_t i = 0; i < 80; ++i) {
        std::uint32_t f = 0;
        std::uint32_t k = 0;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999u;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1u;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDCu;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6u;
        }
        const std::uint32_t t = std::rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = std::rotl(b, 30);
        b = a;
        a = t;
    }
    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
}

void RomDigest::sha256_block_(const std::uint8_t* p) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i)
        w[i] = be32(p + 4 * i);
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 =
            std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 =
            std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::array<std::uint32_t, 8> v = h_;
    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = std::rotr(v[4], 6) ^ std::rotr(v[4], 11) ^ std::rotr(v[4], 25);
        const std::uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
        const std::uint32_t t1 = v[7] + s1 + ch + kSha256K[i] + w[i];
        const std::uint32_t s0 = std::rotr(v[0], 2) ^ std::rotr(v[0], 13) ^ std::rotr(v[0], 22);
        const std::uint32_t mj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
        std::copy_backward(v.begin(), v.end() - 1, v.end());
        v[4] += t1;
        v[0] = t1 + s0 + mj;
    }
    for (std::size_t i = 0; i < 8; ++i)
        h_[i] += v[i];
}

void RomDigest::block_(const std::uint8_t* p) noexcept {
    if (kind_ == DigestKind::Sha1) {
        sha1_block_(p);
    } else {
        sha256_block_(p);
    }
}

void RomDigest::update(std::span<const std::uint8_t> data) noexcept {
    if (kind_ == DigestKind::Md5) return md5_.update(data);
    if (kind_ == DigestKind::Crc32) {
        h_[0] = crc32_update(h_[0], data);
        return;
    }
    std::size_t fill = static_cast<std::size_t>(total_ % 64u);
    total_ += data.size();
    std::size_t i = 0;
    if (fill != 0) {
        const std::size_t n = std::min(data.size(), 64 - fill);
        std::copy_n(data.data(), n, tail_.data() + fill);
        i = n;
        fill += n;
        if (fill < 64) return;
        block_(tail_.data());
    }
    for (; i + 64 <= data.size(); i += 64)
        block_(data.data() + i);
    std::copy(data.begin() + static_cast<std::ptrdiff_t>(i), data.end(), tail_.begin());
}

DigestValue RomDigest::finish() noexcept {
    DigestValue out{.kind = kind_, .len = digest_len(kind_)};
    if (kind_ == DigestKind::Md5) {
        const auto d = md5_.digest();
        std::copy(d.begin(), d.end(), out.bytes.begin());
        return out;
    }
    if (kind_ == DigestKind::Crc32) {
        for (std::size_t i = 0; i < out.len; ++i)
            out.bytes[i] = static_cast<std::uint8_t>(h_[0] >> (24 - 8 * i));
        return out;
    }
    const std::uint64_t bits = total_ * 8u;
    std::size_t fill = static_cast<std::size_t>(total_ % 64u);
    tail_[fill++] = 0x80;
    if (fill > 56) {
        std::fill(tail_.begin() + static_cast<std::ptrdiff_t>(fill), tail_.end(), std::uint8_t{0});
        block_(tail_.data());
        fill = 0;
    }
    std::fill(tail_.begin() + static_cast<std::ptrdiff_t>(fill), tail_.begin() + 56,
              std::uint8_t{0});
    for (std::size_t i = 0; i < 8; ++i)
        tail_[56 + i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
    block_(tail_.data());
    for (std::size_t i = 0; i < out.len; ++i)
        out.bytes[i] = static_cast<std::uint8_t>(h_[i / 4] >> (24 - 8 * (i % 4)));
    return out;
}

}  // namespace mister::cores
