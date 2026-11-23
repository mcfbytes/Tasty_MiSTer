// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/atari8/apt_table.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "hal/reg_field.h"
#include "infra/affine.h"
#include "infra/endian.h"
#include "infra/span_cut.h"

namespace mister::cores::atari8 {

namespace {

using ::mister::infra::affine::operator+;
using ::mister::infra::affine::operator/;

constexpr infra::ByteOffset kMarkAt{1};
constexpr infra::ByteCount kMarkBytes{3};
constexpr std::array<std::byte, 3> kAptMark = {std::byte{'A'}, std::byte{'P'}, std::byte{'T'}};
constexpr infra::ByteOffset kTableBoot{4};
constexpr hal::RegField<std::uint8_t> kBootBits{0, 0x0F};
constexpr std::byte kTableMeta{0x10};

constexpr infra::ByteOffset kMbrStart{0xC2};
constexpr infra::ByteCount kMbrEntry{16};
constexpr infra::ByteCount kMbrLba{4};
constexpr std::byte kAptType{0x7F};
constexpr infra::ByteCount kLongBytes{4};

constexpr infra::ByteOffset kEntriesAt{16};
constexpr infra::ByteCount kEntryBytes{16};
constexpr infra::ByteOffset kKindAt{1};
constexpr infra::ByteOffset kStartAt{2};
constexpr infra::ByteOffset kCountAt{6};
constexpr infra::ByteOffset kIdAt{10};
constexpr infra::ByteOffset kFlagsAt{12};
constexpr infra::ByteOffset kExternalMetaAt{13};
constexpr infra::ByteOffset kDosSkipAt{14};
constexpr infra::ByteCount kIdBytes{2};
constexpr infra::ByteCount kMetaBytes{3};

constexpr hal::RegField<std::uint8_t> kSizeField{0, 0x8F};
constexpr hal::RegField<std::uint8_t> kMetaField{6, 0x1};
constexpr hal::RegField<std::uint8_t> kLockedField{4, 0x3};
constexpr hal::RegField<std::uint8_t> kPresentField{6, 0x1};
constexpr hal::RegField<std::uint8_t> kWriteLockField{7, 0x1};

enum class AptKind : std::uint8_t { Dos = 0x00, External = 0x03 };
constexpr std::byte kDosKind{std::to_underlying(AptKind::Dos)};
constexpr std::byte kExternalKind{std::to_underlying(AptKind::External)};

constexpr std::array<svc::SectorSize, 4> kSizeByCode =
    {svc::SectorSize{}, svc::SectorSize{128}, svc::SectorSize{256}, svc::SectorSize{512}};

struct CardBytes {
    std::uint32_t v = 0;

    [[nodiscard]] static CardBytes of_field(std::span<const std::byte, 4> sector) noexcept {
        constexpr std::uint32_t kCardBytes = kCardSector.v;
        return CardBytes{infra::load_le<std::uint32_t>(sector) * kCardBytes};
    }

    [[nodiscard]] svc::SectorIndex sector() const noexcept {
        constexpr std::uint32_t kCardBytes = kCardSector.v;
        return svc::SectorIndex{v / kCardBytes};
    }

    [[nodiscard]] CardBytes after(CardBytes skip) const noexcept { return CardBytes{v + skip.v}; }

    [[nodiscard]] CardBytes minus(CardBytes skip) const noexcept { return CardBytes{v - skip.v}; }

    [[nodiscard]] static CardBytes of_skip(std::span<const std::byte, 2> field) noexcept {
        constexpr unsigned kCardShift = 9;
        const auto skip = static_cast<std::uint16_t>(
            infra::load_le<std::uint16_t>(field) << kCardShift
        );
        return CardBytes{skip};
    }
};

constexpr CardBytes kOneSector{512};

[[nodiscard]] bool marked(std::span<const std::byte> table) noexcept {
    return std::ranges::equal(
        infra::affine::first(infra::affine::from(table, kMarkAt), kMarkBytes),
        kAptMark
    );
}

[[nodiscard]] std::optional<SioPartition> decode_apt_entry(
    std::span<const std::byte, 16> e,
    bool read_only
) {
    const std::uint8_t flags = std::to_integer<std::uint8_t>(e[0]);
    const std::uint8_t width = kSizeField.get(flags);
    if (width == 0 || width >= kSizeByCode.size())
        return std::nullopt;
    const std::byte kind = infra::affine::from(e, kKindAt).front();
    if (kind != kDosKind && kind != kExternalKind)
        return std::nullopt;
    const std::uint8_t flags12 = std::to_integer<std::uint8_t>(
        infra::affine::from(e, kFlagsAt).front()
    );
    if (!kPresentField.test(flags12))
        return std::nullopt;

    const CardBytes start = CardBytes::of_field(
        infra::affine::from(e, kStartAt).first<kLongBytes.v>()
    );
    const svc::SectorCount count{
        infra::load_le<std::uint32_t>(infra::affine::from(e, kCountAt).first<kLongBytes.v>())
    };
    const SioPartitionId id{
        infra::load_le<std::uint16_t>(infra::affine::from(e, kIdAt).first<kIdBytes.v>())
    };
    SioPartition p{
        .size = kSizeByCode[width],
        .count = count,
        .id = id,
        .read_only = read_only || kLockedField.test(flags) || kWriteLockField.test(flags12),
        .has_meta = kMetaField.test(flags)
    };
    CardBytes at = start;
    CardBytes meta{};
    if (kind == kDosKind) {
        meta = start.minus(kOneSector);
        at = start.after(
            CardBytes::of_skip(infra::affine::from(e, kDosSkipAt).first<kIdBytes.v>())
        );
    } else {
        std::array<std::byte, 4> wide{};
        std::ranges::copy(
            infra::affine::first(infra::affine::from(e, kExternalMetaAt), kMetaBytes),
            wide.begin()
        );
        meta = CardBytes::of_field(std::span<const std::byte, 4>{wide});
    }
    p.first = at.sector();
    p.meta = meta.sector();
    return p;
}

}

std::optional<AptTable> AptTable::find(svc::ISectorImage& image) {
    std::array<std::byte, kCardSector.v> card{};
    std::array<std::byte, kCardSector.v> apt{};
    if (!image.read_sector(svc::SectorAsk{}, card))
        return std::nullopt;
    std::span<const std::byte> table = infra::affine::first(std::span{card}, kHalfBytes);
    if (!marked(table)) {
        const std::span<const std::byte> mbr = infra::affine::from(std::span{card}, kHalfBytes);
        table = mbr;
        constexpr std::size_t kMbrSlots = 4;
        for (std::size_t slot{}; slot < kMbrSlots; ++slot) {
            const infra::ByteOffset at = kMbrStart + kMbrEntry * slot;
            if (infra::affine::from(mbr, at).front() != kAptType)
                continue;
            const CardBytes lba = CardBytes::of_field(
                infra::affine::from(mbr, at + kMbrLba).first<kLongBytes.v>()
            );
            if (!image.read_sector(svc::SectorAsk{.sector = lba.sector()}, apt))
                return std::nullopt;
            table = infra::affine::first(std::span{apt}, kHalfBytes);
            if (!marked(table))
                return std::nullopt;
            break;
        }
    }
    AptTable out;
    std::ranges::copy(table, out.half_.begin());
    if (!out.plain())
        return std::nullopt;
    return out;
}

bool AptTable::plain() const noexcept {
    return half_.front() == kTableMeta || half_.front() == std::byte{};
}

SioUnit AptTable::boot() const noexcept {
    const std::byte raw = infra::affine::from(std::span{half_}, kTableBoot).front();
    return SioUnit{kBootBits.get(std::to_integer<std::uint8_t>(raw))};
}

std::optional<SioPartition> AptTable::entry(SioUnit unit, bool read_only) const {
    std::span<const std::byte> at = infra::affine::from(std::span{half_}, kEntriesAt);
    for (SioUnit u{}; u < unit; u += SioDriveCount{1})
        at = infra::affine::from(at, kEntryBytes);
    return decode_apt_entry(at.first<16>(), read_only);
}

}
