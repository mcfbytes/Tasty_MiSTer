// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "cores/mac/listing_index.h"
#include "infra/byte_count.h"
#include "infra/endian.h"
#include "proto/types.h"

namespace mister::cores::mac {

inline constexpr std::size_t kBlueScsiBlockBytes = 512;

inline constexpr proto::BlockSize kBlueScsiBlock{kBlueScsiBlockBytes};

inline constexpr std::size_t kBlueScsiPayloadAt = 16;
inline constexpr std::size_t kBlueScsiHeadPayload = kBlueScsiBlockBytes - kBlueScsiPayloadAt;

inline constexpr std::size_t kBlueScsiChunkBytes = 65536;

inline constexpr std::size_t kBlueScsiTailBlocks = (kBlueScsiChunkBytes + kBlueScsiPayloadAt - 1)
    / kBlueScsiBlockBytes;
inline constexpr std::size_t kBlueScsiTailBytes = kBlueScsiTailBlocks * kBlueScsiBlockBytes;
inline constexpr std::size_t kBlueScsiRequestBytes = kBlueScsiBlockBytes + kBlueScsiTailBytes;
inline constexpr std::size_t kBlueScsiReplyBytes = kBlueScsiChunkBytes;

inline constexpr std::size_t kBlueScsiReplyBlocks = kBlueScsiReplyBytes / kBlueScsiBlockBytes;
static_assert(kBlueScsiTailBlocks == 128);

inline constexpr std::uint8_t kBlueScsiSignature = 0xB5;
inline constexpr std::size_t kBlueScsiEntryBytes = 40;
inline constexpr std::size_t kBlueScsiNameBytes = 32;

inline constexpr std::size_t kBlueScsiShareNames = 255;
inline constexpr std::size_t kBlueScsiChangerDiscs = 100;

enum class BlueScsiStatus : std::uint8_t { Good = 0x00, Check = 0x02 };
enum class BlueScsiEntryKind : std::uint8_t { Directory = 0x00, File = 0x01 };

[[nodiscard]] constexpr std::byte wire_byte(BlueScsiStatus status) noexcept {
    return static_cast<std::byte>(status);
}

[[nodiscard]] constexpr std::byte wire_byte(BlueScsiEntryKind kind) noexcept {
    return static_cast<std::byte>(kind);
}

[[nodiscard]] constexpr char blue_scsi_ascii(std::byte name_byte, char replacement) noexcept {
    constexpr std::byte kHighBit{0x80};
    return (name_byte & kHighBit) == std::byte{} ? std::to_integer<char>(name_byte) : replacement;
}

[[nodiscard]] constexpr std::array<std::byte, kBlueScsiEntryBytes> blue_scsi_entry(
    ListingIndex index,
    BlueScsiEntryKind kind,
    std::string_view name,
    proto::FileSize size,
    char replacement
) noexcept {
    constexpr std::size_t kNameAt = 2;
    constexpr std::size_t kSizeAt = 36;
    std::array<std::byte, kBlueScsiEntryBytes> out{};
    out[0] = index.wire_byte();
    out[1] = wire_byte(kind);
    for (std::size_t i = 0; i < name.size() && i < kBlueScsiNameBytes && name[i] != '\0'; ++i) {
        out[kNameAt + i] = static_cast<std::byte>(
            blue_scsi_ascii(static_cast<std::byte>(name[i]), replacement)
        );
    }
    infra::store_be<std::uint32_t>(std::span(out).subspan<kSizeAt, 4>(), size.low_word());
    return out;
}

constexpr infra::ByteCount blue_scsi_status_block(
    BlueScsiStatus status,
    infra::ByteCount reply,
    std::span<std::byte> out
) noexcept {
    constexpr std::size_t kHeader = 5;
    constexpr std::size_t kLengthCeiling = 0x1FFFF;
    constexpr unsigned kLowBits = 16;
    if (out.size() < kHeader)
        return {};
    const std::size_t full = std::min(reply.size(), kLengthCeiling);

    enum At : std::size_t { kStatusAt = 0, kSignatureAt = 1, kLengthAt = 2, kLengthTopAt = 4 };

    out[kStatusAt] = wire_byte(status);
    out[kSignatureAt] = std::byte{kBlueScsiSignature};
    infra::store_be<std::uint16_t>(out.subspan<kLengthAt, 2>(), static_cast<std::uint16_t>(full));
    out[kLengthTopAt] = static_cast<std::byte>(full >> kLowBits);
    return infra::ByteCount{kHeader};
}

}
