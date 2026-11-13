// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "infra/error.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/file.h"
#include "svc/slot_path.h"
#include "svc/storage_backend.h"

namespace mister::svc {

class Vfs;

class FileSlotBackend final : public IStorageBackend {
    TASTY_SEAT_RESIDENT(Io);

public:
    explicit FileSlotBackend(const Vfs& vfs) noexcept : vfs_(vfs) {}

    FileSlotBackend(const FileSlotBackend&) = delete;
    FileSlotBackend& operator=(const FileSlotBackend&) = delete;

    [[nodiscard]] Ex<std::span<std::uint8_t>> read_at(
        proto::FileSize at,
        std::span<std::uint8_t> dst
    ) override;
    [[nodiscard]] Ex<std::span<const std::uint8_t>> write_at(
        proto::FileSize at,
        std::span<const std::uint8_t> src
    ) override;
    [[nodiscard]] Ex<proto::FileSize> open_extent(
        proto::SlotIndex slot,
        const BlockCtl::StorageOrder& order
    ) override;
    [[nodiscard]] Ex<proto::FileSize> create(std::span<const std::uint8_t> first) override;
    [[nodiscard]] Ex<void> flush() override;
    void release(proto::SlotIndex slot) noexcept override;

private:
    const Vfs& vfs_;
    SlotPathText path_{};
    std::unique_ptr<IFile> file_{};
};

}
