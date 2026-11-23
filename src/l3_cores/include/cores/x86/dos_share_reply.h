// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <variant>

#include "cores/x86/dos_attr.h"
#include "cores/x86/dos_cursor.h"
#include "cores/x86/dos_long.h"
#include "cores/x86/dos_name83.h"
#include "cores/x86/dos_open_mode.h"
#include "cores/x86/dos_stamp.h"
#include "cores/x86/dos_word.h"
#include "cores/x86/x86_share_key.h"
#include "infra/byte_count.h"
#include "infra/byte_offset.h"
#include "infra/endian.h"
#include "infra/span_cut.h"
#include "proto/types.h"

namespace mister::cores::x86 {

enum class DosError : std::int16_t {
    Unknown = -1,
    None = 0,
    FileNotFound = 2,
    PathNotFound = 3,
    AccessDenied = 5,
    NoMoreFiles = 0x12,
    WriteFault = 29,
};

struct DosAx {
    std::uint16_t v = 0;

    constexpr void store(std::span<std::byte, 2> out) const noexcept {
        infra::store_le<std::uint16_t>(out, v);
    }

    friend constexpr bool operator==(DosAx, DosAx) = default;
};

enum class DosResultKind : std::uint8_t { Error, Ax, kCount };
using DosResultValue = std::variant<DosError, DosAx>;
inline constexpr std::size_t kDosResultArms = static_cast<std::size_t>(DosResultKind::kCount);
static_assert(
    std::variant_size_v<DosResultValue> == kDosResultArms,
    "DosResultValue: one name per arm"
);
template <DosResultKind K, class T>
inline constexpr bool kDosResultArmIs = std::
    is_same_v<std::variant_alternative_t<static_cast<std::size_t>(K), DosResultValue>, T>;
static_assert(
    kDosResultArmIs<DosResultKind::Error, DosError> && kDosResultArmIs<DosResultKind::Ax, DosAx>
);

struct DosResult {
    DosResultValue value{DosError::None};

    friend constexpr bool operator==(const DosResult&, const DosResult&) = default;
};

[[nodiscard]] constexpr DosResult dos_error(DosError error) noexcept {
    return DosResult{.value = error};
}

[[nodiscard]] constexpr DosResult dos_ax(DosAx ax) noexcept {
    return DosResult{.value = ax};
}

inline void store_dos_error(DosError error, std::span<std::byte, 2> out) noexcept {
    infra::store_le<std::int16_t>(out, std::to_underlying(error));
}

inline void store_dos_result(const DosResult& result, std::span<std::byte, 2> out) noexcept {
    if (const auto* error = std::get_if<DosError>(&result.value)) {
        store_dos_error(*error, out);
        return;
    }
    if (const auto* ax = std::get_if<DosAx>(&result.value))
        ax->store(out);
}

inline constexpr std::uint16_t kSectorsPerCluster = 64;
inline constexpr std::uint16_t kBytesPerSector = 512;
inline constexpr std::uint32_t kClusterBytes = std::uint32_t{kSectorsPerCluster}
    * std::uint32_t{kBytesPerSector};
static_assert(kClusterBytes == std::uint32_t{kSectorsPerCluster} * std::uint32_t{kBytesPerSector});
static_assert(kClusterBytes == 32u * 1024u);

struct DosShareReply {

    struct Clusters {
        static constexpr proto::FileSize kBytesPerCluster{kClusterBytes};
        static constexpr std::uint64_t kMost = std::numeric_limits<std::uint16_t>::max();

        std::uint16_t v = 0;

        [[nodiscard]] static constexpr Clusters of_blocks(proto::FileSize bytes) noexcept {
            const std::uint64_t whole = bytes / kBytesPerCluster;
            return Clusters{static_cast<std::uint16_t>(whole < kMost ? whole : kMost)};
        }

        constexpr void store(std::span<std::byte, 2> out) const noexcept {
            infra::store_le<std::uint16_t>(out, v);
        }

        friend constexpr bool operator==(Clusters, Clusters) = default;
    };

    struct Nothing {
        [[nodiscard]] constexpr infra::ByteCount length() const noexcept { return {}; }

        constexpr void store(std::span<std::byte>) const noexcept {}
    };

    struct Written {
        static constexpr infra::ByteOffset kCountAt{0};
        static constexpr infra::ByteCount kBytes{sizeof(std::uint16_t)};

        DosWord count{};

        [[nodiscard]] constexpr infra::ByteCount length() const noexcept { return kBytes; }

        void store(std::span<std::byte> payload) const noexcept {
            count.store(infra::affine::from(payload, kCountAt).first<2>());
        }
    };

    struct Space {
        static constexpr infra::ByteOffset kTotalAt{0};
        static constexpr infra::ByteOffset kSectorAt{2};
        static constexpr infra::ByteOffset kAvailableAt{4};
        static constexpr infra::ByteCount kBytes{3u * sizeof(std::uint16_t)};

        Clusters total{};
        DosWord bytes_per_sector{};
        Clusters available{};

        [[nodiscard]] constexpr infra::ByteCount length() const noexcept { return kBytes; }

        void store(std::span<std::byte> payload) const noexcept {
            total.store(infra::affine::from(payload, kTotalAt).first<2>());
            bytes_per_sector.store(infra::affine::from(payload, kSectorAt).first<2>());
            available.store(infra::affine::from(payload, kAvailableAt).first<2>());
        }
    };

    struct Attribute {
        static constexpr infra::ByteOffset kStampAt{0};
        static constexpr infra::ByteOffset kSizeAt{4};
        static constexpr infra::ByteOffset kAttrAt{8};
        static constexpr infra::ByteOffset kPadAt{9};
        static constexpr infra::ByteCount kBytes{10};

        DosStamp stamp{};
        proto::FileSize size{};
        DosAttr attr{};

        [[nodiscard]] constexpr infra::ByteCount length() const noexcept { return kBytes; }

        void store(std::span<std::byte> payload) const noexcept {
            stamp.store(infra::affine::from(payload, kStampAt).first<4>());
            size.store_le32(infra::affine::from(payload, kSizeAt).first<4>());
            attr.store(infra::affine::from(payload, kAttrAt).first<1>());
            infra::affine::from(payload, kPadAt).first<1>()[0] = std::byte{0};
        }
    };

    struct Opened {
        static constexpr infra::ByteOffset kLeadAt{0};
        static constexpr infra::ByteOffset kNameAt{1};
        static constexpr infra::ByteOffset kStampAt{12};
        static constexpr infra::ByteOffset kSizeAt{16};
        static constexpr infra::ByteOffset kKeyAt{20};
        static constexpr infra::ByteOffset kExtraAt{22};
        static constexpr infra::ByteOffset kModeAt{24};
        static constexpr infra::ByteCount kBytes{25};

        Name83 name{};
        DosStamp stamp{};
        proto::FileSize size{};
        ShareFileKey key{};
        DosWord extra{};
        DosOpenMode mode{};

        [[nodiscard]] constexpr infra::ByteCount length() const noexcept { return kBytes; }

        void store(std::span<std::byte> payload) const noexcept {
            infra::affine::from(payload, kLeadAt).first<1>()[0] = std::byte{0};
            name.store(infra::affine::from(payload, kNameAt).first<Name83::kBytes>());
            stamp.store(infra::affine::from(payload, kStampAt).first<4>());
            size.store_le32(infra::affine::from(payload, kSizeAt).first<4>());
            key.store(infra::affine::from(payload, kKeyAt).first<2>());
            extra.store(infra::affine::from(payload, kExtraAt).first<2>());
            mode.store(infra::affine::from(payload, kModeAt).first<1>());
        }
    };

    struct Found {
        static constexpr infra::ByteOffset kAttrAt{0};
        static constexpr infra::ByteOffset kNameAt{1};
        static constexpr infra::ByteOffset kStampAt{12};
        static constexpr infra::ByteOffset kSizeAt{16};
        static constexpr infra::ByteOffset kKeyAt{20};
        static constexpr infra::ByteOffset kCursorAt{22};
        static constexpr infra::ByteCount kBytes{24};

        DosAttr attr{};
        Name83 name{};
        DosStamp stamp{};
        proto::FileSize size{};
        ShareLockKey key{};
        DosCursor cursor{};

        [[nodiscard]] constexpr infra::ByteCount length() const noexcept { return kBytes; }

        void store(std::span<std::byte> payload) const noexcept {
            attr.store(infra::affine::from(payload, kAttrAt).first<1>());
            name.store(infra::affine::from(payload, kNameAt).first<Name83::kBytes>());
            stamp.store(infra::affine::from(payload, kStampAt).first<4>());
            size.store_le32(infra::affine::from(payload, kSizeAt).first<4>());
            key.store(infra::affine::from(payload, kKeyAt).first<2>());
            cursor.store(infra::affine::from(payload, kCursorAt).first<2>());
        }
    };

    struct Position {
        static constexpr infra::ByteOffset kAt{0};
        static constexpr infra::ByteCount kBytes{sizeof(std::uint32_t)};

        DosLong at{};

        [[nodiscard]] constexpr infra::ByteCount length() const noexcept { return kBytes; }

        void store(std::span<std::byte> payload) const noexcept {
            at.store(infra::affine::from(payload, kAt).first<4>());
        }
    };

    struct Data {
        infra::ByteCount count{};

        [[nodiscard]] constexpr infra::ByteCount length() const noexcept { return count; }

        constexpr void store(std::span<std::byte>) const noexcept {}
    };

    struct Stale {
        static constexpr infra::ByteCount kBytes{128};

        [[nodiscard]] constexpr infra::ByteCount length() const noexcept { return kBytes; }

        constexpr void store(std::span<std::byte>) const noexcept {}
    };

    using Body = std::
        variant<Nothing, Written, Space, Attribute, Opened, Found, Position, Data, Stale>;

    DosResult result{};
    Body body{};
    std::optional<infra::ByteOffset> terminator{};

    std::optional<infra::ByteOffset> split{};
};

static_assert(
    DosShareReply::Clusters::kBytesPerCluster
    == proto::FileSize::of(infra::ByteCount{kClusterBytes})
);

enum class DosBodyKind : std::uint8_t {
    Nothing,
    Written,
    Space,
    Attribute,
    Opened,
    Found,
    Position,
    Data,
    Stale,
    kCount
};
inline constexpr std::size_t kDosBodyArms = static_cast<std::size_t>(DosBodyKind::kCount);
static_assert(
    std::variant_size_v<DosShareReply::Body> == kDosBodyArms,
    "DosShareReply::Body: one name per arm"
);
template <DosBodyKind K, class T>
inline constexpr bool kArmIs = std::
    is_same_v<std::variant_alternative_t<static_cast<std::size_t>(K), DosShareReply::Body>, T>;
static_assert(
    kArmIs<DosBodyKind::Nothing, DosShareReply::Nothing>
    && kArmIs<DosBodyKind::Written, DosShareReply::Written>
    && kArmIs<DosBodyKind::Space, DosShareReply::Space>
    && kArmIs<DosBodyKind::Attribute, DosShareReply::Attribute>
    && kArmIs<DosBodyKind::Opened, DosShareReply::Opened>
    && kArmIs<DosBodyKind::Found, DosShareReply::Found>
    && kArmIs<DosBodyKind::Position, DosShareReply::Position>
    && kArmIs<DosBodyKind::Data, DosShareReply::Data>
    && kArmIs<DosBodyKind::Stale, DosShareReply::Stale>
);

[[nodiscard]] constexpr DosBodyKind dos_body_kind(const DosShareReply::Body& body) noexcept {
    return static_cast<DosBodyKind>(body.index());
}

inline constexpr DosShareReply::Clusters kNoVolumeTotal{10};
inline constexpr DosShareReply::Clusters kNoVolumeAvailable{1};
inline constexpr DosWord kStandardSectorBytes{kBytesPerSector};

inline constexpr DosWord kSpOpenPlain{0};
inline constexpr DosWord kSpOpenOpened{1};
inline constexpr DosWord kSpOpenCreated{2};
inline constexpr DosWord kSpOpenTruncated{3};

}
