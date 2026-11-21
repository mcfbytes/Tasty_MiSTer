// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "infra/byte_count.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/block_geometry.h"
#include "proto/save_stage.h"
#include "proto/types.h"
#include "svc/card_writer.h"
#include "svc/file.h"
#include "svc/slot_path.h"
#include "svc/storage_backend.h"

namespace mister::svc {

class Vfs;
class SaveStageKeeper;

class FileSlotBackend final : public IStorageBackend {
    TASTY_SEAT_RESIDENT(Io);

public:
    FileSlotBackend(const Vfs& vfs, SaveStageKeeper& keeper) noexcept;
    FileSlotBackend(const FileSlotBackend&) = delete;
    FileSlotBackend& operator=(const FileSlotBackend&) = delete;
    FileSlotBackend(FileSlotBackend&&) = delete;
    FileSlotBackend& operator=(FileSlotBackend&&) = delete;
    ~FileSlotBackend() override = default;

    [[nodiscard]] Ex<infra::ByteCount> read_at(
        proto::FileOffset at,
        std::span<std::byte> dst
    ) override;
    [[nodiscard]] Ex<infra::ByteCount> write_at(
        proto::FileOffset at,
        std::span<const std::byte> src
    ) override;
    [[nodiscard]] Ex<proto::FileSize> open_extent(
        proto::SlotIndex slot,
        const BlockCtl::StorageOrder& order
    ) override;

    [[nodiscard]] Ex<proto::FileSize> open_save_extent(
        proto::SlotIndex slot,
        const BlockCtl::StorageOrder& order
    ) override;
    [[nodiscard]] Ex<proto::FileSize> create(std::span<const std::byte> first) override;
    [[nodiscard]] Ex<void> flush() override;
    void release(proto::SlotIndex slot) noexcept override;

private:
    class ISlotFile {
    public:
        [[nodiscard]] virtual Ex<infra::ByteCount> read_at(
            proto::FileOffset at,
            std::span<std::byte> dst
        ) = 0;
        [[nodiscard]] virtual Ex<infra::ByteCount> write_at(
            proto::FileOffset at,
            std::span<const std::byte> src
        ) = 0;
        [[nodiscard]] virtual Ex<proto::FileSize> create(std::span<const std::byte> first) = 0;
        [[nodiscard]] virtual Ex<void> flush() = 0;
        virtual void close() noexcept = 0;

    protected:
        ISlotFile() = default;
        ISlotFile(const ISlotFile&) = default;
        ISlotFile& operator=(const ISlotFile&) = default;
        ~ISlotFile() = default;
    };

    class InPlace final : public ISlotFile {
        TASTY_SEAT_RESIDENT(Io);

    public:
        explicit InPlace(const Vfs& vfs) noexcept : card_(vfs) {}

        [[nodiscard]] bool take_path(const BlockCtl::StorageOrder& order) noexcept;

        [[nodiscard]] std::string_view path() const noexcept { return path_.view(); }

        void make_parent();

        [[nodiscard]] Ex<proto::FileSize> open_file();

        void counted_by(SaveStageKeeper& keeper) noexcept;

        [[nodiscard]] Ex<infra::ByteCount> read_at(
            proto::FileOffset at,
            std::span<std::byte> dst
        ) override;
        [[nodiscard]] Ex<infra::ByteCount> write_at(
            proto::FileOffset at,
            std::span<const std::byte> src
        ) override;
        [[nodiscard]] Ex<proto::FileSize> create(std::span<const std::byte> first) override;
        [[nodiscard]] Ex<void> flush() override;
        void close() noexcept override;

    private:
        CardWriter card_;
        SlotPathText path_{};
        std::unique_ptr<IFile> file_{};
        infra::OptRef<SaveStageKeeper> counted_by_{};
    };

    class Staged final : public ISlotFile {
        TASTY_SEAT_RESIDENT(Io);

    public:
        explicit Staged(SaveStageKeeper& keeper) noexcept : keeper_(keeper) {}

        void hold(proto::StageKey key) noexcept;

        [[nodiscard]] bool outgrown(const Error& e) const noexcept;

        [[nodiscard]] Ex<void> demote();

        [[nodiscard]] Ex<infra::ByteCount> read_at(
            proto::FileOffset at,
            std::span<std::byte> dst
        ) override;
        [[nodiscard]] Ex<infra::ByteCount> write_at(
            proto::FileOffset at,
            std::span<const std::byte> src
        ) override;
        [[nodiscard]] Ex<proto::FileSize> create(std::span<const std::byte> first) override;
        [[nodiscard]] Ex<void> flush() override;
        void close() noexcept override;

    private:
        SaveStageKeeper& keeper_;
        std::optional<proto::StageKey> key_{};
    };

    void close_() noexcept;
    [[nodiscard]] Ex<infra::ByteCount> demote_(
        proto::FileOffset at,
        std::span<const std::byte> src
    );

    SaveStageKeeper& keeper_;
    InPlace in_place_;
    Staged staged_;
    std::reference_wrapper<ISlotFile> file_;
};

using SaveSlotBackends = std::array<FileSlotBackend, proto::kAnnounceableSlots>;
[[nodiscard]] SaveSlotBackends make_save_slot_backends(
    const Vfs& vfs,
    SaveStageKeeper& keeper
) noexcept;

}
