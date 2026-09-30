// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "proto/storage_completion.h"
#include "proto/storage_request.h"
#include "proto/types.h"

namespace mister::proto {

class IStorageChannel {
public:
    virtual ~IStorageChannel() = default;

    [[nodiscard]] virtual StorageSeq submit(const StorageRequest& req) noexcept = 0;

    [[nodiscard]] virtual std::optional<StorageCompletion> reap() noexcept = 0;

    [[nodiscard]] virtual std::span<const std::uint8_t> half(SlotIndex slot,
                                                             ArenaHalf which) const noexcept = 0;
    [[nodiscard]] virtual std::span<std::uint8_t> half_mut(SlotIndex slot,
                                                           ArenaHalf which) noexcept = 0;

protected:
    IStorageChannel() = default;
    IStorageChannel(const IStorageChannel&) = default;
    IStorageChannel& operator=(const IStorageChannel&) = default;
};

}  // namespace mister::proto
