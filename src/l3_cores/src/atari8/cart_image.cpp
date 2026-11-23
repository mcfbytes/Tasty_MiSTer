// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/atari8/cart_image.h"

#include <array>
#include <cstddef>
#include <utility>

#include "cores/atari8/cart_layout.h"
#include "infra/affine.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/span_cut.h"

namespace mister::cores::atari8 {

namespace {

using infra::affine::operator*;
using infra::affine::operator+;
using infra::affine::operator-;
using infra::affine::operator/;
using infra::affine::operator+=;

constexpr infra::ByteCount kOssBlock{0x1000};
constexpr infra::ByteCount kBank{0x2000};
constexpr infra::ByteCount kMirroredSpace{0x8000};
constexpr infra::ByteCount kSuperCartSpace{0x80000};

constexpr std::byte kBountyBobFirstOrder{0x2F};

struct CartBank {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr infra::ByteOffset at(infra::ByteCount unit) const noexcept {
        return infra::ByteOffset{} + unit * std::size_t{v};
    }

    friend constexpr bool operator==(CartBank, CartBank) = default;
};

struct MaskedBlock {
    CartBank block;
    CartBank mask;
};

constexpr std::array<CartBank, 4> kOss034M{CartBank{0}, CartBank{2}, CartBank{1}, CartBank{3}};
constexpr std::array<CartBank, 4> kOss043M{CartBank{0}, CartBank{1}, CartBank{2}, CartBank{3}};
constexpr std::array<CartBank, 4> kTwoChip{CartBank{0}, CartBank{0}, CartBank{1}, CartBank{1}};
constexpr std::array<CartBank, 4> kOneChip{CartBank{0}, CartBank{1}, CartBank{0}, CartBank{1}};
constexpr std::array<CartBank, 6>
    kBountyBobLow{CartBank{0}, CartBank{1}, CartBank{4}, CartBank{4}, CartBank{2}, CartBank{3}};
constexpr std::array<CartBank, 6>
    kBountyBobHigh{CartBank{1}, CartBank{2}, CartBank{0}, CartBank{0}, CartBank{3}, CartBank{4}};

class Layout {
public:
    explicit Layout(std::span<const std::uint8_t> body) noexcept : body_(body) {}

    void append(infra::ByteOffset at, infra::ByteCount len) {
        const std::span<const std::uint8_t> rest = infra::affine::from_clamped(body_, at);
        const std::span<const std::uint8_t> have = infra::affine::first_clamped(rest, len);
        out_.insert(out_.end(), have.begin(), have.end());
        const infra::ByteCount shortfall = len - infra::ByteCount{have.size()};
        for (infra::ByteCount pad{}; pad < shortfall; pad += infra::ByteCount{1})
            out_.push_back(std::uint8_t{0});
    }

    void append_masked(MaskedBlock b) {
        const std::size_t start = out_.size();
        append(b.block.at(kOssBlock), kOssBlock);
        Layout m{body_};
        m.append(b.mask.at(kOssBlock), kOssBlock);
        for (std::size_t i = 0; i < m.out_.size(); ++i)
            out_[start + i] &= m.out_[i];
    }

    [[nodiscard]] infra::ByteCount body_bytes() const noexcept {
        return infra::ByteCount::of(body_.size());
    }

    [[nodiscard]] std::byte first_byte() const noexcept {
        return body_.empty() ? std::byte{} : std::byte{body_.front()};
    }

    [[nodiscard]] std::vector<std::uint8_t> release() && { return std::move(out_); }

private:
    std::span<const std::uint8_t> body_;
    std::vector<std::uint8_t> out_;
};

void oss(Layout& l, const std::array<CartBank, 4>& order) {
    constexpr std::size_t kThird = 2;
    for (const CartBank block : order)
        l.append(block.at(kOssBlock), kOssBlock);
    l.append_masked({.block = order[0], .mask = order[kThird]});
    l.append_masked({.block = order[1], .mask = order[kThird]});
}

void banks(Layout& l, std::span<const CartBank> order) {
    for (const CartBank bank : order)
        l.append(bank.at(kBank), kBank);
}

}

std::vector<std::uint8_t> lay_out_cart(
    const CartMatch& match,
    CartForm form,
    std::span<const std::uint8_t> file
) {
    constexpr infra::ByteCount kCarHeaderSkip{kCarHeaderBytes.file_word<std::uint64_t>()};
    const infra::ByteCount header = form == CartForm::Car ? kCarHeaderSkip : infra::ByteCount{};
    Layout l{header.past_in(file)};
    const infra::ByteCount body = l.body_bytes();
    switch (match.layout) {
        case CartLayout::Plain:
        case CartLayout::Corina:
            l.append(infra::ByteOffset{}, body);
            break;
        case CartLayout::Oss034M:
            oss(l, kOss034M);
            break;
        case CartLayout::Oss043M:
            oss(l, kOss043M);
            break;
        case CartLayout::TwoChip:
            banks(l, kTwoChip);
            break;
        case CartLayout::OneChip:
            banks(l, kOneChip);
            break;
        case CartLayout::Mirrored:
            for (std::size_t i = 0; !body.empty() && i < kMirroredSpace / body; ++i)
                l.append(infra::ByteOffset{}, body);
            break;
        case CartLayout::SuperCart:
            for (std::size_t i = 0; !body.empty() && i < kSuperCartSpace / body; ++i)
                for (std::size_t j = 0; j < body / kBank; ++j)
                    l.append(infra::ByteOffset{} + j * kBank, kBank);
            break;
        case CartLayout::BountyBob:
            banks(l, l.first_byte() == kBountyBobFirstOrder ? kBountyBobLow : kBountyBobHigh);
            break;
    }
    return std::move(l).release();
}

}
