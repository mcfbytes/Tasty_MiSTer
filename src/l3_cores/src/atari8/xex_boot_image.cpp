// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/atari8/xex_boot_image.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>

#include "cores/atari8/xex_loaders.h"
#include "infra/affine.h"
#include "infra/byte_offset.h"
#include "infra/span_cut.h"

namespace mister::cores::atari8 {

using infra::affine::operator*;
using infra::affine::operator/;

namespace {

constexpr svc::SectorCount kLoaderSectors = XexBootImage::kSectorSize.whole_in(
    proto::FileSize::of(std::span<const std::byte>{kXexBootSectors})
);

constexpr std::array<std::byte, 3> dos2_vtoc_head(svc::SectorCount disk) noexcept {
    constexpr svc::SectorCount kVtocSpan{1024};
    constexpr svc::SectorCount kVtocTail{943};
    constexpr svc::SectorCount kVtocReserve{12};
    constexpr svc::SectorCount kOne{1};
    constexpr svc::SectorCount kTwo{2};
    constexpr svc::SectorCount kThree{3};
    const std::uint32_t spans = disk / kVtocSpan;
    svc::SectorCount vtoc{spans};
    const svc::SectorCount rest = disk - spans * kVtocSpan;
    if (rest > kVtocTail) {
        vtoc += kTwo;
    } else if (rest != svc::SectorCount{}) {
        vtoc += kOne;
    }

    if ((vtoc / kTwo) * kTwo == vtoc)
        vtoc += kOne;
    const svc::SectorCount freed = disk - (vtoc + kVtocReserve);
    const std::array<std::byte, 4> bytes = freed.le_bytes();
    return {svc::SectorCount{(vtoc + kThree) / kTwo}.le_bytes()[0], bytes[0], bytes[1]};
}

constexpr svc::SectorCount kDirectoryLarge{0x28F};
constexpr std::byte kEntryLarge{0x46};
constexpr std::byte kEntrySmall{0x42};
constexpr char kBlank = ' ';
constexpr char kUnknown = '@';

constexpr char spelt(char c) noexcept {
    if (c >= 'a' && c <= 'z')
        return static_cast<char>(c - ('a' - 'A'));
    if (c < 'A' || c > 'Z')
        return kUnknown;
    return c;
}

struct Dos2SectorFill {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr Dos2SectorFill of(infra::ByteCount n) noexcept {
        return Dos2SectorFill{static_cast<std::uint8_t>(n.v)};
    }

    [[nodiscard]] constexpr std::byte byte() const noexcept { return std::byte{v}; }

    friend constexpr bool operator==(Dos2SectorFill, Dos2SectorFill) = default;
};

struct Dos2Link {
    std::optional<SioSector> next;
    Dos2SectorFill used;

    static constexpr infra::ByteOffset kNextHigh{0};
    static constexpr infra::ByteOffset kNextLow{1};
    static constexpr infra::ByteOffset kUsedAt{2};

    void encode(std::span<std::byte, 3> out) const noexcept {
        const std::array<std::byte, 2> le = next ? next->le_bytes() : std::array<std::byte, 2>{};
        infra::affine::from(out, kNextHigh).front() = le[1];
        infra::affine::from(out, kNextLow).front() = le[0];
        infra::affine::from(out, kUsedAt).front() = used.byte();
    }
};

struct Dos2DirEntry {
    std::string_view name;
    svc::SectorCount sectors;

    static constexpr infra::ByteOffset kFlagsAt{0};
    static constexpr infra::ByteOffset kCountAt{1};
    static constexpr infra::ByteOffset kStartAt{3};
    static constexpr infra::ByteOffset kNameAt{5};
    static constexpr infra::ByteCount kNameStem{8};

    static constexpr infra::ByteCount kNameLetters{11};

    static constexpr infra::ByteCount kLastStem{7};
    static constexpr infra::ByteCount kExtension{3};

    [[nodiscard]] constexpr std::byte flags() const noexcept {
        return sectors > kDirectoryLarge ? kEntryLarge : kEntrySmall;
    }

    void encode(std::span<std::byte, 16> out) const noexcept {
        std::ranges::fill(
            infra::affine::first(infra::affine::from(out, kNameAt), kNameStem),
            std::byte{kBlank}
        );
        const infra::ByteCount length{name.size()};
        infra::ByteOffset from{};
        for (infra::ByteCount i{}; i < kNameLetters; i += infra::ByteCount{1}) {
            const char c = from.inside(name) ? from.element_at(name) : '\0';
            if (c == '.') {
                i = kLastStem;
            } else {
                infra::affine::from(out, kNameAt + i).front() = std::byte{
                    static_cast<unsigned char>(spelt(c))
                };
            }
            from = i == kLastStem
                ? infra::ByteOffset{} + (length >= kExtension ? length - kExtension : length)
                : from + infra::ByteCount{1};
        }
        infra::affine::from(out, kFlagsAt).front() = flags();
        const std::array<std::byte, 4> count = sectors.le_bytes();
        const std::span<std::byte> count_at = infra::affine::from(out, kCountAt);
        count_at[0] = count[0];
        count_at[1] = count[1];
        const std::array<std::byte, 2> start = XexBootImage::kFirstFileSector.le_bytes();
        const std::span<std::byte> start_at = infra::affine::from(out, kStartAt);
        start_at[0] = start[0];
        start_at[1] = start[1];
    }
};

constexpr infra::ByteOffset kEntryEnd{16};

}

Ex<std::unique_ptr<svc::ISectorImage>> XexBootImage::open(
    std::unique_ptr<svc::IFile> file,
    std::string_view name
) {
    TASTY_SEAT_BODY(XexBootImage);
    if (file == nullptr)
        return std::unexpected(Error{Errc::contract, ERR_SITE(), 0});
    const auto size = file->size();
    if (!size)
        return std::unexpected(size.error());
    return std::unique_ptr<svc::ISectorImage>(
        new XexBootImage(std::move(file), std::string{name}, *size)
    );
}

svc::SectorGeometry XexBootImage::geometry() const noexcept {
    TASTY_SEAT_BODY(XexBootImage);
    return svc::SectorGeometry{
        .size = kSectorSize,
        .count = kStructureSectors + kLinkData.covering(size_)
    };
}

bool XexBootImage::writable() const noexcept {
    TASTY_SEAT_BODY(XexBootImage);
    return false;
}

Ex<svc::SectorRead> XexBootImage::read_sector(const svc::SectorAsk& ask, std::span<std::byte> dst) {
    TASTY_SEAT_BODY(XexBootImage);
    if (!kSectorSize.fits(dst))
        return std::unexpected(Error{Errc::contract, ERR_SITE(), 0});
    const std::span<std::byte> sector = kSectorSize.blank(dst);
    const SioSector number = SioSector::at(
        ask.sector - svc::SectorIndex{},
        SioSectorOrigin::FromOne
    );
    const svc::SectorRead read{.bytes = kSectorSize.bytes()};
    if (infra::affine::in_bounds(kLoaderSectors, ask.sector)) {
        std::ranges::copy(
            kSectorSize.sector_in(std::span<const std::byte>{kXexBootSectors}, ask.sector),
            sector.begin()
        );
    } else if (number == kVtocSector) {
        vtoc_(sector);
    } else if (number == kDirectorySector) {
        directory_(sector);
    } else if (number >= kFirstFileSector) {
        if (auto got = file_sector_(number, sector); !got)
            return std::unexpected(got.error());
    }
    return read;
}

Ex<svc::SectorRead> XexBootImage::write_sector(
    const svc::SectorAsk& ask,
    std::span<const std::byte>
) {
    TASTY_SEAT_BODY(XexBootImage);
    return std::unexpected(ask.sector.refusal(Errc::contract, ERR_SITE()));
}

Ex<void> XexBootImage::clear() {
    TASTY_SEAT_BODY(XexBootImage);
    return std::unexpected(Error{Errc::contract, ERR_SITE(), 0});
}

void XexBootImage::vtoc_(std::span<std::byte> sector) const noexcept {
    std::ranges::copy(dos2_vtoc_head(geometry().count), sector.begin());
}

void XexBootImage::directory_(std::span<std::byte> sector) const noexcept {
    Dos2DirEntry{.name = name_, .sectors = geometry().count - kStructureSectors}.encode(
        sector.first<16>()
    );
    std::ranges::fill(infra::affine::from(sector, kEntryEnd), std::byte{});
}

Ex<void> XexBootImage::file_sector_(SioSector number, std::span<std::byte> sector) {
    const proto::FileOffset at = proto::kFileStart + kLinkData.span_of(number - kFirstFileSector);
    std::span<std::byte> rest = kLinkData.of(sector);
    infra::ByteCount got{};
    while (!rest.empty()) {
        const auto n = file_->read_at(at + proto::FileSize::of(got), rest);
        if (!n)
            return std::unexpected(n.error());
        if (n->empty())
            break;
        got += *n;
        rest = n->past_in(rest);
    }
    const std::optional<SioSector> next = got >= kLinkData.bytes()
        ? std::optional<SioSector>{number + svc::SectorCount{1}}
        : std::nullopt;
    Dos2Link{.next = next, .used = Dos2SectorFill::of(got)}.encode(sector.last<3>());
    return {};
}

}
