// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/atari8/cart_matches.h"

#include <algorithm>
#include <array>
#include <cstddef>

#include "cores/atari8/cart_kib.h"
#include "cores/atari8/cart_size_test.h"
#include "cores/atari8/cart_type.h"
#include "infra/byte_offset.h"
#include "infra/span_cut.h"

namespace mister::cores::atari8 {

namespace {

using Bytes = std::span<const std::byte>;

constexpr std::array<std::byte, 4>
    kCartMagic{std::byte{'C'}, std::byte{'A'}, std::byte{'R'}, std::byte{'T'}};
constexpr std::array<std::byte, 3> kSdxMagic{std::byte{'S'}, std::byte{'D'}, std::byte{'X'}};

[[nodiscard]] std::byte byte_at(Bytes head, infra::ByteOffset at) noexcept {
    const Bytes rest = infra::affine::from_clamped(head, at);
    return rest.empty() ? std::byte{} : rest.front();
}

template <std::size_t N>
[[nodiscard]] bool starts_with(Bytes head, const std::array<std::byte, N>& magic) noexcept {
    for (std::size_t i = 0; i < magic.size(); ++i)
        if (byte_at(head, infra::ByteOffset{i}) != magic[i])
            return false;
    return true;
}

constexpr infra::ByteOffset kSdxKindByte{0x1FF3};
constexpr std::byte kSdxSide2Kind{0xE1};
constexpr std::byte kSdxU1mbKind{0xE0};

[[nodiscard]] bool size_fits(const CartRow& row, CartSizeTest test, proto::FileSize size) noexcept {
    switch (test) {
        case CartSizeTest::WholeKib:
            return row.size == CartKib::floor_of(size);
        case CartSizeTest::Exact:
            break;
    }
    return row.size.bytes() == size;
}

CartMatches by_type(const CartTable& table, Bytes head) {
    CartMatches out;
    if (!starts_with(head, kCartMagic))
        return out;
    const CartType type{std::to_integer<std::uint8_t>(byte_at(head, CartType::kHeaderByte))};
    const auto row = std::ranges::find(table.rows, type, &CartRow::type);
    if (row != table.rows.end())
        (void)out.add(CartMatch::of(*row));
    return out;
}

CartMatches by_size(const CartTable& table, Bytes head, proto::FileSize size) {
    CartMatches out;
    const std::byte kind = byte_at(head, kSdxKindByte);
    if (table.sdx_signature
        && proto::FileSize::of(head) >= kRawHeadBytes
        && starts_with(head, kSdxMagic)
        && (kind == kSdxSide2Kind || kind == kSdxU1mbKind)) {
        (void)out.add(CartMatch{.mode = kind == kSdxSide2Kind ? kSdxSide2Mode : kSdxU1mbMode});
        return out;
    }
    for (const CartRow& row : table.rows)
        if (size_fits(row, table.size_test, size))
            (void)out.add(CartMatch::of(row));
    return out;
}

}

Ex<CartMatch> CartMatches::pick(LoadChoice choice) const {
    if (matches_.empty())
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    if (matches_.size() == 1 && choice.none())
        return matches_.front();
    if (choice.none())
        return std::unexpected(Error{Errc::choice_needed, ERR_SITE(), 0});
    const auto picked = choice.among(all());
    if (!picked)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), 0});
    return *picked;
}

CartMatches match_carts(
    const CartTable& table,
    CartForm form,
    std::span<const std::uint8_t> head,
    proto::FileSize size
) {
    const Bytes bytes = std::as_bytes(head);
    switch (form) {
        case CartForm::Car:
            return by_type(table, bytes);
        case CartForm::Raw:
            break;
    }
    return by_size(table, bytes, size);
}

}
