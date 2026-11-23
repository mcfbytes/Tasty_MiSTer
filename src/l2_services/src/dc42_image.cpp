// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/dc42_image.h"

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

constexpr proto::FileSize kHeaderBytes = Dc42Image::kPayload - proto::kFileStart;

}

std::optional<Dc42Image::Header> Dc42Image::Header::probe(
    Bytes head,
    proto::FileSize file_size
) noexcept {

    if (file_size < kHeaderBytes || proto::FileSize::of(head) < kHeaderBytes)
        return std::nullopt;
    constexpr std::size_t kNameMax = 63;
    constexpr proto::FileOffset kDataAt{0x40};
    constexpr proto::FileOffset kTagsAt{0x44};
    constexpr std::size_t kMagicAt = 0x52;
    constexpr std::array kMagic{std::byte{0x01}, std::byte{0x00}};
    constexpr proto::FileSize kBlock{512};
    constexpr proto::FileSize kTagBytes{12};
    constexpr ImageField<proto::FileSize, std::uint32_t, std::endian::big> kDataField{kDataAt};
    constexpr ImageField<proto::FileSize, std::uint32_t, std::endian::big> kTagsField{kTagsAt};
    if (std::to_integer<std::size_t>(head[0]) > kNameMax)
        return std::nullopt;
    const proto::FileSize data = kDataField.read(head);
    const proto::FileSize tags = kTagsField.read(head);
    if (head[kMagicAt] != kMagic[0] || head[kMagicAt + 1] != kMagic[1])
        return std::nullopt;
    if (data.empty() || !data.bytes_past_whole(kBlock).empty())
        return std::nullopt;
    if (!tags.bytes_past_whole(kTagBytes).empty())
        return std::nullopt;
    if (kHeaderBytes + data + tags != file_size)
        return std::nullopt;
    return Header{.data = data};
}

Dc42Image::Dc42Image(std::unique_ptr<IDiskImage> file, proto::FileSize extent) noexcept
    : file_(std::move(file)),
      extent_(extent) {}

Ex<std::unique_ptr<IDiskImage>> Dc42Image::open(
    std::unique_ptr<IDiskImage> file,
    PayloadReach reach
) {
    TASTY_SEAT_BODY(Dc42Image);
    std::array<std::byte, kHeaderBytes.file_word<std::size_t>()> head{};
    auto got = file->read_at(proto::kFileStart, head);
    if (!got)
        return std::unexpected(got.error());
    const proto::FileSize size = file->size();
    const std::optional<Header> h = Header::probe(
        got->first_in(std::span<const std::byte>(head)),
        size
    );
    if (!h)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});

    const proto::FileSize extent = reach == PayloadReach::Declared ? h->data
                                                                   : kPayload.remaining_in(size);
    return std::unique_ptr<IDiskImage>{new Dc42Image(std::move(file), extent)};
}

FileSize Dc42Image::size() const noexcept {
    TASTY_SEAT_BODY(Dc42Image);
    return extent_;
}

bool Dc42Image::writable() const noexcept {
    TASTY_SEAT_BODY(Dc42Image);
    return file_->writable();
}

Ex<infra::ByteCount> Dc42Image::read_at(proto::FileOffset at, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(Dc42Image);
    if (!extent_.holds(at))
        return infra::ByteCount{};
    const proto::FileSize into = at - proto::kFileStart;
    return file_->read_at(kPayload + into, infra::affine::first_clamped(dst, extent_ - into));
}

Ex<infra::ByteCount> Dc42Image::write_at(proto::FileOffset at, std::span<const std::byte> src) {
    TASTY_SEAT_BODY(Dc42Image);
    return file_->write_at(kPayload + (at - proto::kFileStart), src);
}

}
