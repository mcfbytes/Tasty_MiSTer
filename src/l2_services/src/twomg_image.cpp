// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/twomg_image.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>

#include "svc/image_field.h"

namespace mister::svc {
namespace {

using Bytes = std::span<const std::byte>;

constexpr std::array kMagic{std::byte{'2'}, std::byte{'I'}, std::byte{'M'}, std::byte{'G'}};

constexpr proto::FileSize kHeaderBytes{64};
constexpr proto::BlockSize kProDosBlock{512};

}

std::optional<TwoMgImage::Header> TwoMgImage::Header::parse(
    Bytes head,
    proto::FileSize file_size
) noexcept {

    if (file_size < kHeaderBytes || proto::FileSize::of(head) < kHeaderBytes)
        return std::nullopt;
    if (!std::ranges::equal(head.first(kMagic.size()), kMagic))
        return std::nullopt;
    constexpr proto::FileOffset kLengthAt{8};
    constexpr proto::FileOffset kFormatAt{12};
    constexpr proto::FileOffset kBlocksAt{20};
    constexpr proto::FileOffset kOffsetAt{24};
    constexpr proto::FileOffset kDataAt{28};
    constexpr ImageField<proto::FileSize, std::uint16_t, std::endian::little> kLengthField{
        kLengthAt
    };
    constexpr ImageField<Order, std::uint32_t, std::endian::little> kFormatField{kFormatAt};
    constexpr ImageField<proto::BlockCount, std::uint32_t, std::endian::little> kBlocksField{
        kBlocksAt
    };
    constexpr ImageField<proto::FileSize, std::uint32_t, std::endian::little> kOffsetField{
        kOffsetAt
    };
    constexpr ImageField<proto::FileSize, std::uint32_t, std::endian::little> kDataField{kDataAt};
    const proto::FileSize stated = kLengthField.read(head);
    const proto::FileSize offset = kOffsetField.read(head);
    proto::FileSize data = kDataField.read(head);

    if (data.empty())
        data = proto::FileSize{kProDosBlock.window_bytes(kBlocksField.read(head))};
    if (stated != kHeaderBytes || offset < kHeaderBytes || offset + data > file_size)
        return std::nullopt;
    return Header{
        .order = kFormatField.read(head),
        .payload = proto::kFileStart + offset,
        .declared = data
    };
}

TwoMgImage::TwoMgImage(
    std::unique_ptr<IDiskImage> file,
    proto::FileOffset payload,
    proto::FileSize extent
) noexcept
    : file_(std::move(file)),
      payload_(payload),
      extent_(extent) {}

Ex<std::unique_ptr<IDiskImage>> TwoMgImage::open(
    std::unique_ptr<IDiskImage> file,
    PayloadReach reach
) {
    TASTY_SEAT_BODY(TwoMgImage);
    std::array<std::byte, kHeaderBytes.file_word<std::size_t>()> head{};
    auto got = file->read_at(proto::kFileStart, head);
    if (!got)
        return std::unexpected(got.error());
    const proto::FileSize size = file->size();
    const std::optional<Header> h = Header::parse(
        got->first_in(std::span<const std::byte>(head)),
        size
    );
    if (!h)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});

    const proto::FileSize past = h->payload.remaining_in(size);
    const proto::FileSize extent = reach == PayloadReach::Declared ? h->declared
                                                                   : (past.empty() ? size : past);
    return std::unique_ptr<IDiskImage>{new TwoMgImage(std::move(file), h->payload, extent)};
}

FileSize TwoMgImage::size() const noexcept {
    TASTY_SEAT_BODY(TwoMgImage);
    return extent_;
}

bool TwoMgImage::writable() const noexcept {
    TASTY_SEAT_BODY(TwoMgImage);
    return file_->writable();
}

Ex<infra::ByteCount> TwoMgImage::read_at(proto::FileOffset at, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(TwoMgImage);
    if (!extent_.holds(at))
        return infra::ByteCount{};
    const proto::FileSize into = at - proto::kFileStart;
    return file_->read_at(payload_ + into, infra::affine::first_clamped(dst, extent_ - into));
}

Ex<infra::ByteCount> TwoMgImage::write_at(proto::FileOffset at, std::span<const std::byte> src) {
    TASTY_SEAT_BODY(TwoMgImage);
    return file_->write_at(payload_ + (at - proto::kFileStart), src);
}

}
