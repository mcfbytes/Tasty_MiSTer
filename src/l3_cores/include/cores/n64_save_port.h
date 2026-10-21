// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/storage_backend.h"

namespace mister::cores::n64 {

class SaveChannelStore;

class SavePort final : public svc::IStorageBackend {
    TASTY_SEAT_RESIDENT(Io);

public:
    SavePort(SaveChannelStore& store, std::uint8_t index) noexcept
        : store_(&store),
          index_(index) {}

    SavePort(const SavePort&) = delete;
    SavePort& operator=(const SavePort&) = delete;

    [[nodiscard]] Ex<std::size_t> read_at(
        std::uint64_t offset,
        std::span<std::uint8_t> dst
    ) override;
    [[nodiscard]] Ex<std::size_t> write_at(
        std::uint64_t offset,
        std::span<const std::uint8_t> src
    ) override;
    [[nodiscard]] Ex<proto::FileSize> open_extent(std::span<const std::byte> order) override;
    void note_tag(std::uint8_t tag) noexcept override;
    void release() noexcept override;

private:
    SaveChannelStore* store_;
    std::uint8_t index_;
};

}
