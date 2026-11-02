// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "proto/block_geometry.h"
#include "proto/image_source.h"
#include "proto/types.h"

namespace mister::proto {

class IBlockServer : public IImageSource {
public:
    [[nodiscard]] virtual Ex<std::size_t> read_at(
        SlotIndex slot,
        std::uint64_t offset,
        std::span<std::uint8_t> dst
    ) = 0;
    [[nodiscard]] virtual Ex<std::size_t> write_at(
        SlotIndex slot,
        std::uint64_t offset,
        std::span<const std::uint8_t> src
    ) = 0;

    [[nodiscard]] virtual Ex<FileSize> create(SlotIndex slot, std::span<const std::uint8_t>) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), slot.v});
    }

    [[nodiscard]] virtual BlockGeometry geometry_for(SlotIndex, Lba, BlockGeometry wire) {
        return wire;
    }

protected:
    IBlockServer() = default;
    IBlockServer(const IBlockServer&) = default;
    IBlockServer& operator=(const IBlockServer&) = default;
};

}
