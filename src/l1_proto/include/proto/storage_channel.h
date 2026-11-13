// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "proto/resident_window.h"
#include "proto/storage_completion.h"
#include "proto/storage_request.h"
#include "proto/types.h"

namespace mister::proto {

class IStorageChannel {
public:
    struct HandsOut {
        BlockSlotMask data{};
        BlockSlotMask ctl{};
    };

    struct HomeWindow {
        ResidentWindow window;
        std::span<const std::uint8_t> bytes;
    };

    virtual ~IStorageChannel() = default;

    [[nodiscard]] virtual std::optional<StorageSeq> submit(const StorageRequest& req) noexcept = 0;

    [[nodiscard]] virtual std::optional<StorageCompletion> reap() noexcept = 0;

    [[nodiscard]] virtual HandsOut hands_out() const noexcept = 0;

    [[nodiscard]] virtual std::optional<HomeWindow> window(SlotIndex) const noexcept = 0;

    [[nodiscard]] virtual std::span<std::uint8_t> claim(SlotIndex) noexcept = 0;

    virtual void install(SlotIndex, const ResidentWindow&) noexcept = 0;

protected:
    IStorageChannel() = default;
    IStorageChannel(const IStorageChannel&) = default;
    IStorageChannel& operator=(const IStorageChannel&) = default;
};

}
