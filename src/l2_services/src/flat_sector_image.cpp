// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/flat_sector_image.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "infra/endian.h"

namespace mister::svc {

namespace {

constexpr SectorSize kSingle{128};
constexpr SectorSize kDouble{256};
constexpr SectorSize kWide{512};

constexpr SectorCount kShortBoot{3};

constexpr std::size_t kClearChunk = 4096;

struct AtrParagraphs {
    std::uint32_t v = 0;

    [[nodiscard]] static AtrParagraphs decode(std::span<const std::byte, 16> h) noexcept {
        constexpr std::size_t kLowWordAt = 2;
        constexpr std::size_t kHighByteAt = 6;
        constexpr unsigned kLowWordBits = 16;
        const auto low = infra::load_le<std::uint16_t>(
            h.subspan<kLowWordAt, sizeof(std::uint16_t)>()
        );
        const auto high = std::to_integer<std::uint32_t>(h[kHighByteAt]);
        return AtrParagraphs{low | (high << kLowWordBits)};
    }

    [[nodiscard]] proto::FileSize bytes() const noexcept {
        constexpr std::uint32_t kParagraphBytes = 16;
        return proto::FileSize{std::uint64_t{v} * kParagraphBytes};
    }

    [[nodiscard]] SectorCount wide_sectors() const noexcept {
        constexpr std::uint32_t kParagraphsPerSector = 32;
        return SectorCount{v / kParagraphsPerSector};
    }
};

struct AtrHeader {
    AtrParagraphs paragraphs{};
    SectorSize size{};

    [[nodiscard]] static AtrHeader decode(std::span<const std::byte, 16> h) noexcept {
        return AtrHeader{
            .paragraphs = AtrParagraphs::decode(h),
            .size = SectorSize{infra::load_le<std::uint16_t>(h.subspan<4, 2>())}
        };
    }

    [[nodiscard]] bool serves() const noexcept {
        return size == kSingle || size == kDouble || size == kWide;
    }

    [[nodiscard]] SectorCount count() const noexcept {
        if (size == kWide)
            return paragraphs.wide_sectors();
        const proto::FileSize bytes = paragraphs.bytes();
        const proto::FileSize lead = SectorGeometry::kShortLead.span_of(kShortBoot);
        return bytes >= lead ? kShortBoot + size.whole_in(bytes - lead)
                             : kShortBoot - size.whole_in(lead - bytes);
    }

    [[nodiscard]] SectorGeometry geometry() const noexcept {
        return SectorGeometry{
            .size = size,
            .count = count(),
            .short_lead = size == kWide ? SectorCount{} : kShortBoot
        };
    }
};

[[nodiscard]] Ex<void> read_filled(IFile& file, proto::FileOffset at, std::span<std::byte> dst) {
    std::ranges::fill(dst, std::byte{});
    while (!dst.empty()) {
        const auto n = file.read_at(at, dst);
        if (!n)
            return std::unexpected(n.error());
        if (n->empty())
            return {};
        at += proto::FileSize::of(*n);
        dst = n->past_in(dst);
    }
    return {};
}

}

Ex<std::unique_ptr<ISectorImage>> FlatSectorImage::open(
    std::unique_ptr<IFile> file,
    bool writable,
    SectorSize headerless
) {
    TASTY_SEAT_BODY(FlatSectorImage);
    if (file == nullptr)
        return std::unexpected(Error{Errc::contract, ERR_SITE(), 0});
    const auto size = file->size();
    if (!size)
        return std::unexpected(size.error());
    std::array<std::byte, kAtrHeaderBytes.v> head{};
    if (auto got = read_filled(*file, proto::kFileStart, head); !got)
        return std::unexpected(got.error());
    const bool atr = std::ranges::equal(std::span{head}.first<std::size(kAtrMagic)>(), kAtrMagic);
    if (!atr) {
        const SectorGeometry flat{
            .size = headerless,
            .count = headerless.whole_in(*size),
            .short_lead = headerless == kWide ? SectorCount{} : kShortBoot
        };
        return std::unique_ptr<ISectorImage>(
            new FlatSectorImage(std::move(file), writable, proto::kFileStart, flat)
        );
    }
    const AtrHeader header = AtrHeader::decode(head);
    if (!header.serves())
        return std::unexpected(header.size.refusal(Errc::bad_format, ERR_SITE()));
    return std::unique_ptr<ISectorImage>(new FlatSectorImage(
        std::move(file),
        writable,
        proto::kFileStart + kAtrHeaderBytes,
        header.geometry()
    ));
}

SectorGeometry FlatSectorImage::geometry() const noexcept {
    TASTY_SEAT_BODY(FlatSectorImage);
    return geometry_;
}

bool FlatSectorImage::writable() const noexcept {
    TASTY_SEAT_BODY(FlatSectorImage);
    return writable_;
}

proto::FileOffset FlatSectorImage::place_of_(SectorIndex at) const noexcept {
    const SectorCount lead = geometry_.short_lead;
    if (infra::affine::in_bounds(lead, at))
        return first_ + SectorGeometry::kShortLead.span_of(at - SectorIndex{});
    return first_
        + SectorGeometry::kShortLead.span_of(lead)
        + geometry_.size.span_of(at - infra::affine::end_of<SectorIndex>(lead));
}

Ex<SectorRead> FlatSectorImage::read_sector(const SectorAsk& ask, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(FlatSectorImage);
    const SectorSize size = geometry_.size_of(ask.sector);
    if (!size.fits(dst))
        return std::unexpected(size.refusal(Errc::contract, ERR_SITE()));
    if (auto got = read_filled(*file_, place_of_(ask.sector), size.of(dst)); !got)
        return std::unexpected(got.error());
    return SectorRead{.bytes = size.bytes()};
}

Ex<SectorRead> FlatSectorImage::write_sector(const SectorAsk& ask, std::span<const std::byte> src) {
    TASTY_SEAT_BODY(FlatSectorImage);
    if (!writable_)
        return std::unexpected(ask.sector.refusal(Errc::contract, ERR_SITE()));
    const SectorSize size = geometry_.size_of(ask.sector);
    if (!size.fits(src))
        return std::unexpected(size.refusal(Errc::contract, ERR_SITE()));
    const proto::FileOffset at = place_of_(ask.sector);
    if (auto put = file_->write_all_at(at, size.of(src)); !put)
        return std::unexpected(put.error());
    if (auto synced = file_->flush(); !synced)
        return std::unexpected(synced.error());
    SectorRead done{.bytes = size.bytes()};
    if (ask.check == SectorCheck::None)
        return done;
    std::array<std::byte, kWide.v> back{};
    const std::span<std::byte> readback = size.of(std::span<std::byte>{back});
    if (auto got = read_filled(*file_, at, readback); !got)
        return std::unexpected(got.error());
    if (!std::ranges::equal(readback, size.of(src)))
        done.outcome = SectorOutcome::Faulty;
    return done;
}

Ex<void> FlatSectorImage::clear() {
    TASTY_SEAT_BODY(FlatSectorImage);
    if (!writable_)
        return std::unexpected(Error{Errc::contract, ERR_SITE(), 0});
    const auto size = file_->size();
    if (!size)
        return std::unexpected(size.error());
    static constexpr std::array<std::byte, kClearChunk> kZeros{};
    for (proto::FileOffset at = first_; at.remaining_in(*size) != proto::FileSize{};) {
        const std::span<const std::byte> part = std::span{kZeros}.first(
            size->clamp_write(at, kZeros.size())
        );
        if (auto put = file_->write_all_at(at, part); !put)
            return std::unexpected(put.error());
        at += proto::FileSize::of(part);
    }
    return file_->flush();
}

}
