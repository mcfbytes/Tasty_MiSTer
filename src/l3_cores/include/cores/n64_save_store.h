// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "cores/n64_save_order.h"
#include "cores/n64_save_plan.h"
#include "cores/n64_memory_type.h"
#include "cores/n64_save_extent.h"
#include "cores/types.h"
#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/block_geometry.h"
#include "proto/types.h"
#include "svc/file.h"
#include "svc/slot_path.h"
#include "svc/storage_backend.h"
#include "svc/vfs.h"

namespace mister::cores::n64 {

class SaveChannelStore final : public svc::IStorageBackend {
    TASTY_SEAT_RESIDENT(Io);

public:
    static constexpr std::size_t kMaxSliceBytes = proto::kBlockStagingBytes;

    static constexpr std::size_t kMaxPartitionBytes = 0x20000;

    SaveChannelStore(const svc::Vfs& vfs) noexcept;
    SaveChannelStore(const SaveChannelStore&) = delete;
    SaveChannelStore& operator=(const SaveChannelStore&) = delete;

    [[nodiscard]] Ex<proto::FileSize> open_extent(
        proto::SlotIndex slot,
        std::span<const std::byte> order
    ) override;

    void release(proto::SlotIndex slot) noexcept override;

    void note_tag(proto::ImageTag tag) noexcept override;

    [[nodiscard]] Ex<std::size_t> read_at(
        std::uint64_t offset,
        std::span<std::uint8_t> dst
    ) override;
    [[nodiscard]] Ex<std::size_t> write_at(
        std::uint64_t offset,
        std::span<const std::uint8_t> src
    ) override;

    [[nodiscard]] std::uint32_t unchanged_writes() const noexcept { return unchanged_; }

    [[nodiscard]] std::uint32_t stray_requests() const noexcept { return stray_; }

    [[nodiscard]] std::uint32_t clamped_writes() const noexcept { return clamped_; }

    [[nodiscard]] std::uint32_t layout_errors() const noexcept { return layout_errors_; }

    [[nodiscard]] std::uint32_t creates() const noexcept { return creates_; }

    [[nodiscard]] std::uint32_t migrated() const noexcept { return migrated_; }

    [[nodiscard]] bool holds(SaveRow row) const noexcept {
        const auto part = parts_.find(row);
        return part && part->is_open();
    }

private:
    class OpenPartition {
        TASTY_SEAT_RESIDENT(Io);

    public:
        void claim(proto::SlotIndex slot) noexcept { slot_ = slot; }

        [[nodiscard]] bool claimed_by(proto::SlotIndex slot) const noexcept {
            return slot_ == slot;
        }

        void release() noexcept {
            slot_.reset();
            close();
        }

        [[nodiscard]] Ex<proto::FileSize> open(const svc::Vfs& vfs, std::string_view path);

        void close() noexcept {
            file_.reset();
            on_disk_ = proto::FileSize{};
        }

        [[nodiscard]] bool is_open() const noexcept { return file_ != nullptr; }

        [[nodiscard]] infra::OptRef<svc::IFile> file() noexcept {
            return infra::opt_ref_of(file_.get());
        }

        [[nodiscard]] proto::FileSize on_disk() const noexcept { return on_disk_; }

    private:
        std::unique_ptr<svc::IFile> file_{};
        std::optional<proto::SlotIndex> slot_{};
        proto::FileSize on_disk_{};
    };

    void adopt_(const SavePlan& plan) noexcept;

    [[nodiscard]] Ex<std::size_t> render_(const SaveOrder& o);
    [[nodiscard]] bool migrate_(const SaveOrder& o);
    [[nodiscard]] Ex<void> create_(const SaveOrder& o, std::size_t bytes);

    const svc::Vfs* vfs_;

    SavePlan plan_{};
    SaveLayout layout_{};
    proto::ImageTag key_{};
    infra::IndexedArray<OpenPartition, kMaxSaveRows, SaveRow> parts_{};
    svc::SlotPathText tmp_{};
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
