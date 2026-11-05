// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "proto/block_geometry.h"
#include "proto/block_server.h"
#include "proto/types.h"

namespace mister::proto {

enum class SlotServer : std::uint8_t { Session, CoreBlock, CoreDescriptor, kCount };

class BlockBacking {
public:
    struct Session {};

    struct CoreBlock {
        infra::OptRef<IBlockServer> server;
    };

    struct CoreDescriptor {
        infra::OptRef<IImageSource> descriptor;
    };

    using Served = std::variant<Session, CoreBlock, CoreDescriptor>;

    static_assert(
        std::variant_size_v<Served> == static_cast<std::size_t>(SlotServer::kCount),
        "BlockBacking: one SlotServer name per arm"
    );
    template <SlotServer K, class T>
    static constexpr bool
        kArmIs = std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(K), Served>, T>;
    static_assert(
        kArmIs<SlotServer::Session, Session>
        && kArmIs<SlotServer::CoreBlock, CoreBlock>
        && kArmIs<SlotServer::CoreDescriptor, CoreDescriptor>
    );

    constexpr BlockBacking() noexcept = default;

    [[nodiscard]] static constexpr BlockBacking session() noexcept { return BlockBacking{}; }

    [[nodiscard]] static constexpr BlockBacking core_block(
        infra::OptRef<IBlockServer> bytes
    ) noexcept {
        return BlockBacking{CoreBlock{bytes}};
    }

    [[nodiscard]] static constexpr BlockBacking core_descriptor(
        infra::OptRef<IImageSource> descriptor
    ) noexcept {
        return BlockBacking{CoreDescriptor{descriptor}};
    }

    [[nodiscard]] constexpr SlotServer kind() const noexcept {
        return static_cast<SlotServer>(served_.index());
    }

    [[nodiscard]] constexpr infra::OptRef<IImageSource> source() const noexcept {
        if (const auto* b = std::get_if<CoreBlock>(&served_))
            return b->server ? infra::OptRef<IImageSource>{*b->server}
                             : infra::OptRef<IImageSource>{};
        if (const auto* d = std::get_if<CoreDescriptor>(&served_))
            return d->descriptor;
        return {};
    }

    [[nodiscard]] constexpr infra::OptRef<IBlockServer> server() const noexcept {
        if (const auto* b = std::get_if<CoreBlock>(&served_))
            return b->server;
        return {};
    }

private:
    explicit constexpr BlockBacking(Served served) noexcept : served_(served) {}

    Served served_{Session{}};
};

using SlotServers = infra::IndexedArray<SlotServer, kBlockSlots, SlotIndex>;

[[nodiscard]] inline SlotServer slot_server_of(
    const SlotServers& servers,
    SlotIndex slot
) noexcept {
    return slot.v < kBlockSlots ? servers[slot] : SlotServer::Session;
}

}
