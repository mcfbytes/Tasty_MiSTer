// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "cores/n64_save_order.h"
#include "cores/n64_save_port.h"
#include "cores/n64_save_type.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "proto/block_geometry.h"
#include "proto/types.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::cores::n64 {

class SaveChannelStore {
    TASTY_SEAT_RESIDENT(Io);

public:
    static constexpr std::size_t kMaxSliceBytes = proto::kBlockStagingBytes;

    static constexpr std::size_t kMaxPartitionBytes = 0x20000;

    SaveChannelStore(const svc::Vfs& vfs) noexcept;
    SaveChannelStore(const SaveChannelStore&) = delete;
    SaveChannelStore& operator=(const SaveChannelStore&) = delete;

    [[nodiscard]] Ex<proto::FileSize> open(std::size_t i, std::span<const std::byte> order);
    void close(std::size_t i) noexcept;

    void note_key(std::uint8_t key) noexcept;

    [[nodiscard]] Ex<std::size_t> read_at(std::uint64_t offset, std::span<std::uint8_t> dst);
    [[nodiscard]] Ex<std::size_t> write_at(std::uint64_t offset, std::span<const std::uint8_t> src);

    [[nodiscard]] std::uint32_t unchanged_writes() const noexcept { return unchanged_; }

    [[nodiscard]] std::uint32_t stray_requests() const noexcept { return stray_; }

    [[nodiscard]] std::uint32_t clamped_writes() const noexcept { return clamped_; }

    [[nodiscard]] std::uint32_t layout_errors() const noexcept { return layout_errors_; }

    [[nodiscard]] std::uint32_t creates() const noexcept { return creates_; }

    [[nodiscard]] std::uint32_t migrated() const noexcept { return migrated_; }

    [[nodiscard]] bool holds(std::size_t i) const noexcept {
        return i < kMaxSaveRows && files_[i] != nullptr;
    }

private:
    void seed_(std::size_t i) noexcept;

    [[nodiscard]] Ex<std::size_t> render_(const SaveOrder& b, std::size_t i);
    [[nodiscard]] bool migrate_(const SaveOrder& b, std::size_t i);
    [[nodiscard]] Ex<void> create_(const SaveOrder& b, std::size_t bytes);

    const svc::Vfs* vfs_;

    SaveOrder rows_[kMaxSaveRows]{};
    bool byteswap_[kMaxSaveRows]{};
    std::unique_ptr<svc::IFile> files_[kMaxSaveRows]{};
    std::uint32_t on_disk_[kMaxSaveRows]{};
    std::uint32_t offset_[kMaxSaveRows]{};
    std::uint32_t length_[kMaxSaveRows]{};
    svc::SlotPathText tmp_{};
    std::uint8_t count_ = 0;
    std::uint8_t key_ = 0;
    std::uint32_t unchanged_ = 0;
    std::uint32_t stray_ = 0;
    std::uint32_t clamped_ = 0;
    std::uint32_t layout_errors_ = 0;
    std::uint32_t creates_ = 0;
    std::uint32_t migrated_ = 0;
    std::uint8_t blank_[kMaxPartitionBytes]{};
    std::uint8_t scratch_[kMaxSliceBytes]{};
    std::uint8_t compare_[kMaxSliceBytes]{};
};

}
