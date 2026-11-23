// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "cores/mac/blue_scsi_wire.h"
#include "cores/mac/listing_index.h"
#include "infra/byte_count.h"
#include "infra/endian.h"
#include "proto/types.h"

namespace mister::cores::mac {

class BlueScsiCdb {
public:
    [[nodiscard]] static constexpr std::optional<BlueScsiCdb> of(
        std::span<const std::byte> request
    ) noexcept {
        if (request.size() < kBlueScsiBlockBytes)
            return std::nullopt;
        return BlueScsiCdb{
            request.first<kBlueScsiBlockBytes>(),
            request.subspan(kBlueScsiBlockBytes)
        };
    }

    [[nodiscard]] constexpr std::byte opcode() const noexcept { return block_[kOpcodeAt]; }

    [[nodiscard]] constexpr ListingIndex index() const noexcept {
        return ListingIndex::of(block_[kIndexAt]);
    }

    [[nodiscard]] constexpr proto::Lba wide_block() const noexcept {
        return proto::Lba{infra::load_be<std::uint32_t>(block_.subspan<kWideBlockAt, 4>())};
    }

    [[nodiscard]] constexpr proto::Lba block() const noexcept {
        return proto::Lba{
            infra::load_be<std::uint32_t>(block_.subspan<kWideBlockAt, 4>()) & kBlockMask
        };
    }

    [[nodiscard]] constexpr proto::BlockCount blocks() const noexcept {
        return proto::BlockCount{std::to_integer<std::uint32_t>(block_[kBlocksAt])};
    }

    [[nodiscard]] constexpr infra::ByteCount byte_count() const noexcept {
        return infra::ByteCount{infra::load_be<std::uint16_t>(block_.subspan<kByteCountAt, 2>())};
    }

    [[nodiscard]] constexpr std::span<const std::byte, kBlueScsiHeadPayload>
    payload() const noexcept {
        return block_.subspan<kBlueScsiPayloadAt>();
    }

    [[nodiscard]] constexpr std::span<const std::byte> tail() const noexcept { return tail_; }

private:
    enum Layout : std::size_t {
        kOpcodeAt = 0,
        kIndexAt = 1,
        kByteCountAt = 1,
        kWideBlockAt = 2,
        kBlocksAt = 6,
    };

    static constexpr std::uint32_t kBlockMask = 0x00FF'FFFF;

    constexpr BlueScsiCdb(
        std::span<const std::byte, kBlueScsiBlockBytes> block,
        std::span<const std::byte> tail
    ) noexcept
        : block_(block),
          tail_(tail) {}

    std::span<const std::byte, kBlueScsiBlockBytes> block_;
    std::span<const std::byte> tail_;
};

}
