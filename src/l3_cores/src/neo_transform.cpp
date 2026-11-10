// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/neo_transform.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "infra/endian.h"

namespace mister::cores::neogeo {

namespace {

inline constexpr WordCount kSprBlock{32};
inline constexpr WordCount kDblBlock{64};

[[nodiscard]] WordCount load_words(
    std::span<const std::uint8_t> piece,
    std::span<std::uint16_t> src
) noexcept {
    const WordCount words = WordCount::of(piece);
    std::ranges::fill(src, std::uint16_t{0});
    infra::load_le_run<std::uint16_t>(piece, words.head_of(src));
    return words;
}

}

void FixShape::transform(std::span<std::uint8_t> piece) noexcept {
    TASTY_SEAT_BODY(FixShape);
    const std::size_t size = piece.size();

    src_.assign(block32_extent(size), std::uint8_t{0});
    out_.resize(size);
    std::ranges::copy(piece, src_.begin());
    for (std::size_t i = 0; i < size; ++i)
        out_[i] = src_[fix_index(i)];
    std::ranges::copy(out_, piece.begin());
}

void SprShape::transform(std::span<std::uint8_t> piece) noexcept {
    TASTY_SEAT_BODY(SprShape);
    src_ = WordCount::of(piece).rounded_up_to(kSprBlock).sized_vector<std::uint16_t>();
    const WordCount words = load_words(piece, std::span(src_));
    out_ = words.sized_vector<std::uint16_t>();
    for (std::uint32_t i = 0; words.holds(i); ++i)
        out_[i] = src_[spr_index(i)];
    infra::store_le_run<std::uint16_t>(words.head_of(std::span<const std::uint16_t>(out_)), piece);
}

void SprShape::transform_skip(
    std::span<const std::uint16_t> words,
    std::span<std::uint16_t> out,
    WordCount words_n
) noexcept {
    TASTY_SEAT_BODY(SprShape);
    const auto src = words_n.rounded_up_to(kSprBlock).first_of(words);
    const auto dst = words_n.halfword_extent().first_of(out);
    for (std::uint32_t i = 0; words_n.holds(i); ++i)
        dst[i << 1] = src[spr_index(i)];
}

void SprDblShape::transform(std::span<std::uint8_t> piece) noexcept {
    TASTY_SEAT_BODY(SprDblShape);
    src_ = WordCount::of(piece).rounded_up_to(kDblBlock).sized_vector<std::uint16_t>();
    const WordCount words = load_words(piece, std::span(src_));
    out_ = words.sized_vector<std::uint16_t>();
    for (std::uint32_t i = 0; words.holds(i); ++i)
        out_[i] = src_[spr_dbl_index(i)];
    infra::store_le_run<std::uint16_t>(words.head_of(std::span<const std::uint16_t>(out_)), piece);
}

void SprDblSwapShape::transform(std::span<std::uint8_t> piece) noexcept {
    TASTY_SEAT_BODY(SprDblSwapShape);

    dwords_.resize(piece.size() / 4u);
    infra::load_le_run<std::uint32_t>(piece, dwords_);

    constexpr std::uint32_t kOuterBytes = 0xFF0000FFu;
    constexpr std::uint32_t kMidBytes = 0xFF00u;
    constexpr std::uint32_t kMidHighByte = 0xFF0000u;
    constexpr unsigned kMidByteShift = 8;
    for (std::uint32_t& w : dwords_)
        w = (w & kOuterBytes)
            | ((w & kMidBytes) << kMidByteShift)
            | ((w & kMidHighByte) >> kMidByteShift);
    infra::store_le_run<std::uint32_t>(dwords_, piece);

    src_ = WordCount::of(piece).rounded_up_to(kDblBlock).sized_vector<std::uint16_t>();
    const WordCount words = load_words(piece, std::span(src_));
    out_ = words.sized_vector<std::uint16_t>();
    for (std::uint32_t i = 0; words.holds(i); ++i)
        out_[i] = src_[spr_dbl_index(i)];
    infra::store_le_run<std::uint16_t>(words.head_of(std::span<const std::uint16_t>(out_)), piece);
}

}
