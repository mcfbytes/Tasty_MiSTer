// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/rom_digest.h"

#include <algorithm>
#include <bit>
#include <cstddef>

#include "infra/crc32.h"
#include "infra/endian.h"

namespace mister::cores {
namespace {

constexpr std::array<std::uint32_t, 5>
    kSha1Init{0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
constexpr std::array<std::uint32_t, 8> kSha256Init{
    0x6a09e667u,
    0xbb67ae85u,
    0x3c6ef372u,
    0xa54ff53au,
    0x510e527fu,
    0x9b05688cu,
    0x1f83d9abu,
    0x5be0cd19u
};
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
    0xc67178f2u
};

std::uint32_t be32(const std::uint8_t* p) {
    constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
    return infra::load_be<std::uint32_t>(std::span<const std::uint8_t, kWordBytes>(p, kWordBytes));
}

int hex(char c) {
    constexpr int kHexLetterBase = 10;
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + kHexLetterBase;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + kHexLetterBase;
    return -1;
}

}

std::optional<DigestValue> parse_hex_digest(std::string_view text, DigestKind kind) noexcept {
    constexpr std::size_t kHexPerByte = 2;
    constexpr unsigned kNibbleBits = 4;
    DigestValue v{.kind = kind, .len = digest_len(kind)};
    if (text.size() != kHexPerByte * v.len)
        return std::nullopt;
    for (std::size_t i = 0; i < v.len; ++i) {
        const int hi = hex(text[kHexPerByte * i]);
        const int lo = hex(text[kHexPerByte * i + 1]);
        if (hi < 0 || lo < 0)
            return std::nullopt;
        v.bytes[i] = static_cast<std::uint8_t>(hi << kNibbleBits | lo);
    }
    return v;
}

RomDigest::RomDigest(DigestKind kind) noexcept : kind_(kind) {
    switch (kind_) {
        case DigestKind::Sha1:
            std::copy(kSha1Init.begin(), kSha1Init.end(), h_.begin());
            break;
        case DigestKind::Sha256:
            h_ = kSha256Init;
            break;
        case DigestKind::Md5:
        case DigestKind::Crc32:
            break;
    }
}

void RomDigest::sha1_block_(const std::uint8_t* p) noexcept {
    constexpr std::size_t kBlockWords = 16;
    constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
    constexpr std::size_t kSha1Rounds = 80;
    constexpr std::size_t kSha1Stage = 20;
    constexpr std::uint32_t kSha1K0 = 0x5A827999u;
    constexpr std::uint32_t kSha1K1 = 0x6ED9EBA1u;
    constexpr std::uint32_t kSha1K2 = 0x8F1BBCDCu;
    constexpr std::uint32_t kSha1K3 = 0xCA62C1D6u;
    constexpr std::size_t kSha1Stage2 = 2 * kSha1Stage;
    constexpr std::size_t kSha1Stage3 = 3 * kSha1Stage;
    constexpr int kSha1StepRot = 5;
    constexpr int kSha1BRot = 30;
    constexpr std::size_t kSha1Back3 = 3;
    constexpr std::size_t kSha1Back8 = 8;
    constexpr std::size_t kSha1Back14 = 14;
    constexpr std::size_t kSha1Back16 = 16;
    std::array<std::uint32_t, kSha1Rounds> w{};
    for (std::size_t i = 0; i < kBlockWords; ++i)
        w[i] = be32(p + kWordBytes * i);
    for (std::size_t i = kBlockWords; i < kSha1Rounds; ++i)
        w[i] = std::rotl(
            w[i - kSha1Back3] ^ w[i - kSha1Back8] ^ w[i - kSha1Back14] ^ w[i - kSha1Back16],
            1
        );
    std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4];
    for (std::size_t i = 0; i < kSha1Rounds; ++i) {
        std::uint32_t f = 0;
        std::uint32_t k = 0;
        if (i < kSha1Stage) {
            f = (b & c) | (~b & d);
            k = kSha1K0;
        } else if (i < kSha1Stage2) {
            f = b ^ c ^ d;
            k = kSha1K1;
        } else if (i < kSha1Stage3) {
            f = (b & c) | (b & d) | (c & d);
            k = kSha1K2;
        } else {
            f = b ^ c ^ d;
            k = kSha1K3;
        }
        const std::uint32_t t = std::rotl(a, kSha1StepRot) + f + e + k + w[i];
        e = d;
        d = c;
        c = std::rotl(b, kSha1BRot);
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
    constexpr std::size_t kBlockWords = 16;
    constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
    constexpr std::size_t kSha256Rounds = 64;
    constexpr std::size_t kSha256Words = 8;
    constexpr std::size_t kSha256Back15 = 15;
    constexpr std::size_t kSha256Back2 = 2;
    constexpr std::size_t kSha256Back16 = 16;
    constexpr std::size_t kSha256Back7 = 7;
    constexpr int kSigma0Rot1 = 7;
    constexpr int kSigma0Rot2 = 18;
    constexpr int kSigma0Shift = 3;
    constexpr int kSigma1Rot1 = 17;
    constexpr int kSigma1Rot2 = 19;
    constexpr int kSigma1Shift = 10;
    constexpr int kBigSigma1Rot1 = 6;
    constexpr int kBigSigma1Rot2 = 11;
    constexpr int kBigSigma1Rot3 = 25;
    constexpr int kBigSigma0Rot1 = 2;
    constexpr int kBigSigma0Rot2 = 13;
    constexpr int kBigSigma0Rot3 = 22;
    constexpr std::size_t kVa = 0;
    constexpr std::size_t kVb = 1;
    constexpr std::size_t kVc = 2;
    constexpr std::size_t kVe = 4;
    constexpr std::size_t kVf = 5;
    constexpr std::size_t kVg = 6;
    constexpr std::size_t kVh = 7;
    std::array<std::uint32_t, kSha256Rounds> w{};
    for (std::size_t i = 0; i < kBlockWords; ++i)
        w[i] = be32(p + kWordBytes * i);
    for (std::size_t i = kBlockWords; i < kSha256Rounds; ++i) {
        const std::uint32_t x = w[i - kSha256Back15];
        const std::uint32_t y = w[i - kSha256Back2];
        const std::uint32_t s0 = std::rotr(x, kSigma0Rot1)
            ^ std::rotr(x, kSigma0Rot2)
            ^ (x >> kSigma0Shift);
        const std::uint32_t s1 = std::rotr(y, kSigma1Rot1)
            ^ std::rotr(y, kSigma1Rot2)
            ^ (y >> kSigma1Shift);
        w[i] = w[i - kSha256Back16] + s0 + w[i - kSha256Back7] + s1;
    }
    std::array<std::uint32_t, kSha256Words> v = h_;
    for (std::size_t i = 0; i < kSha256Rounds; ++i) {
        const std::uint32_t e = v[kVe];
        const std::uint32_t a = v[kVa];
        const std::uint32_t s1 = std::rotr(e, kBigSigma1Rot1)
            ^ std::rotr(e, kBigSigma1Rot2)
            ^ std::rotr(e, kBigSigma1Rot3);
        const std::uint32_t ch = (e & v[kVf]) ^ (~e & v[kVg]);
        const std::uint32_t t1 = v[kVh] + s1 + ch + kSha256K[i] + w[i];
        const std::uint32_t s0 = std::rotr(a, kBigSigma0Rot1)
            ^ std::rotr(a, kBigSigma0Rot2)
            ^ std::rotr(a, kBigSigma0Rot3);
        const std::uint32_t mj = (a & v[kVb]) ^ (a & v[kVc]) ^ (v[kVb] & v[kVc]);
        std::copy_backward(v.begin(), v.end() - 1, v.end());
        v[kVe] += t1;
        v[kVa] = t1 + s0 + mj;
    }
    for (std::size_t i = 0; i < kSha256Words; ++i)
        h_[i] += v[i];
}

void RomDigest::block_(const std::uint8_t* p) noexcept {
    switch (kind_) {
        case DigestKind::Sha1:
            sha1_block_(p);
            return;
        case DigestKind::Sha256:
            sha256_block_(p);
            return;
        case DigestKind::Md5:
        case DigestKind::Crc32:
            return;
    }
}

void RomDigest::update(std::span<const std::uint8_t> data) noexcept {
    constexpr std::size_t kBlockBytes = 64;
    switch (kind_) {
        case DigestKind::Md5:
            md5_.update(data);
            return;
        case DigestKind::Crc32:
            h_[0] = crc32_update(h_[0], data);
            return;
        case DigestKind::Sha1:
        case DigestKind::Sha256:
            break;
    }
    std::size_t fill = static_cast<std::size_t>(total_ % kBlockBytes);
    total_ += data.size();
    std::size_t i = 0;
    if (fill != 0) {
        const std::size_t n = std::min(data.size(), kBlockBytes - fill);
        std::copy_n(data.data(), n, tail_.data() + fill);
        i = n;
        fill += n;
        if (fill < kBlockBytes)
            return;
        block_(tail_.data());
    }
    for (; i + kBlockBytes <= data.size(); i += kBlockBytes)
        block_(data.data() + i);
    std::copy(data.begin() + static_cast<std::ptrdiff_t>(i), data.end(), tail_.begin());
}

DigestValue RomDigest::finish() noexcept {
    constexpr std::size_t kBlockBytes = 64;
    constexpr std::size_t kLengthAt = 56;
    constexpr std::size_t kLengthBytes = 8;
    constexpr std::uint8_t kPadMarker = 0x80;
    constexpr unsigned kByteBits = 8;
    constexpr std::size_t kWordBytes = sizeof(std::uint32_t);
    constexpr unsigned kTopByteShift = 24;
    constexpr unsigned kTopBitShift = 56;
    DigestValue out{.kind = kind_, .len = digest_len(kind_)};
    switch (kind_) {
        case DigestKind::Md5: {
            const auto d = md5_.digest();
            std::copy(d.begin(), d.end(), out.bytes.begin());
            return out;
        }
        case DigestKind::Crc32: {
            for (std::size_t i = 0; i < out.len; ++i)
                out.bytes[i] = static_cast<std::uint8_t>(h_[0] >> (kTopByteShift - kByteBits * i));
            return out;
        }
        case DigestKind::Sha1:
        case DigestKind::Sha256:
            break;
    }
    const std::uint64_t bits = total_ * kByteBits;
    std::size_t fill = static_cast<std::size_t>(total_ % kBlockBytes);
    tail_[fill++] = kPadMarker;
    if (fill > kLengthAt) {
        std::fill(tail_.begin() + static_cast<std::ptrdiff_t>(fill), tail_.end(), std::uint8_t{0});
        block_(tail_.data());
        fill = 0;
    }
    std::fill(
        tail_.begin() + static_cast<std::ptrdiff_t>(fill),
        tail_.begin() + kLengthAt,
        std::uint8_t{0}
    );
    for (std::size_t i = 0; i < kLengthBytes; ++i)
        tail_[kLengthAt + i] = static_cast<std::uint8_t>(bits >> (kTopBitShift - kByteBits * i));
    block_(tail_.data());
    for (std::size_t i = 0; i < out.len; ++i)
        out.bytes[i] = static_cast<std::uint8_t>(
            h_[i / kWordBytes] >> (kTopByteShift - kByteBits * (i % kWordBytes))
        );
    return out;
}

}
