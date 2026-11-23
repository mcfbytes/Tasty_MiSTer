// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/byte_count.h"
#include "infra/error.h"
#include "infra/prelude.h"
#include "proto/track_shape.h"
#include "proto/types.h"
#include "svc/block_ctl.h"

namespace mister::svc {

class IStorageBackend {
public:
    virtual ~IStorageBackend() = default;

    [[nodiscard]] virtual Ex<infra::ByteCount> read_at(
        proto::FileOffset at,
        std::span<std::byte> dst
    ) = 0;

    [[nodiscard]] virtual Ex<infra::ByteCount> write_at(
        proto::FileOffset at,
        std::span<const std::byte> src
    ) = 0;

    [[nodiscard]] virtual Ex<proto::FileSize> open_extent(
        proto::SlotIndex slot,
        const BlockCtl::StorageOrder& order
    ) = 0;

    [[nodiscard]] virtual Ex<proto::FileSize> open_save_extent(
        proto::SlotIndex slot,
        const BlockCtl::StorageOrder& order
    ) {
        return open_extent(slot, order);
    }

    [[nodiscard]] virtual Ex<proto::FileSize> create(std::span<const std::byte>) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 0});
    }

    [[nodiscard]] virtual Ex<void> flush() { return {}; }

    [[nodiscard]] virtual proto::TrackShape track_shape() const noexcept { return {}; }

    [[nodiscard]] virtual Ex<infra::ByteCount> read_track(
        const proto::TrackShape::Ask&,
        std::span<std::byte>
    ) {
        return std::unexpected(Error{Errc::contract, ERR_SITE(), 0});
    }

    [[nodiscard]] virtual Ex<void> write_track(
        const proto::TrackShape::Ask&,
        std::span<const std::byte>
    ) {
        return std::unexpected(Error{Errc::contract, ERR_SITE(), 0});
    }

    virtual void note_tag(proto::ImageTag) noexcept {}

    virtual void release(proto::SlotIndex slot) noexcept = 0;

protected:
    IStorageBackend() = default;
    IStorageBackend(const IStorageBackend&) = default;
    IStorageBackend& operator=(const IStorageBackend&) = default;
};

}
