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
#include "svc/vfs.h"

namespace mister::svc {

class FileSlotBackend final : public IStorageBackend {
    TASTY_SEAT_RESIDENT(Io);

public:
    explicit FileSlotBackend(const Vfs& vfs) noexcept : vfs_(&vfs) {}
    FileSlotBackend(const FileSlotBackend&) = delete;
    FileSlotBackend& operator=(const FileSlotBackend&) = delete;

    [[nodiscard]] Ex<std::size_t> read_at(std::uint64_t offset,
                                          std::span<std::uint8_t> dst) override;
    [[nodiscard]] Ex<std::size_t> write_at(std::uint64_t offset,
                                           std::span<const std::uint8_t> src) override;
    [[nodiscard]] Ex<proto::FileSize> open_extent(std::span<const std::byte> order) override;
    [[nodiscard]] Ex<proto::FileSize> create(std::span<const std::uint8_t> first) override;
    [[nodiscard]] Ex<void> flush() override;
    void release() noexcept override;

private:
    const Vfs* vfs_;
    SlotPathText path_{};
    std::unique_ptr<IFile> file_{};
};

}  // namespace mister::svc
