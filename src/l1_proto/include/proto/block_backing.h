// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "infra/opt_ref.h"
#include "proto/block_geometry.h"
#include "proto/types.h"

namespace mister::proto {

class IBlockServer;
class IImageSource;

enum class SlotServer : std::uint8_t { Session, CoreBlock, CoreDescriptor };

struct BlockBacking {
    SlotServer kind = SlotServer::Session;
    infra::OptRef<IBlockServer> server{};
    infra::OptRef<IImageSource> descriptor{};
};

[[nodiscard]] inline SlotServer slot_server_of(
    const std::array<SlotServer, kBlockSlots>& servers,
    SlotIndex slot
) noexcept {
    return slot.v < kBlockSlots ? servers[slot.v] : SlotServer::Session;
}

}
