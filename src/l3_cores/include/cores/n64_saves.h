// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "cores/n64_save_layout.h"
#include "cores/n64_save_order.h"
#include "cores/n64_save_port.h"
#include "cores/n64_save_store.h"
#include "cores/n64_save_type.h"
#include "cores/nvram_store.h"
#include "cores/save_channel.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/image_source.h"
#include "svc/storage_backend.h"
#include "svc/vfs.h"

namespace mister::cores::n64 {

class SaveChannel final : public ISaveChannel, public proto::IImageSource {
    TASTY_SEAT_RESIDENT(RT);

public:
    static constexpr std::size_t kMaxPartitionBytes = SaveChannelStore::kMaxPartitionBytes;

    static constexpr std::size_t kPakBytes = 0x8000;
    static constexpr std::size_t kPathChars = SavePath::kCapacity;

    explicit SaveChannel(const svc::Vfs& vfs) noexcept;
    SaveChannel(const SaveChannel&) = delete;
    SaveChannel& operator=(const SaveChannel&) = delete;

    [[nodiscard]] Ex<void> plan(
        const ISaveLayout& live,
        std::string_view image_path,
        std::string_view core_dir,
        std::span<const SavePartition> declared
    );
    void clear() noexcept;

    [[nodiscard]] std::uint64_t channel_bytes() const noexcept;

    [[nodiscard]] std::uint32_t name_refusals() const noexcept { return name_refusals_; }

    [[nodiscard]] std::uint32_t opens() const noexcept { return opens_; }

    [[nodiscard]] std::uint32_t open_failures() const noexcept { return open_failures_; }

    [[nodiscard]] SaveChannelStore& store() noexcept { return io_; }

    [[nodiscard]] std::optional<std::size_t> claim_mount_plan() noexcept override;
    [[nodiscard]] Ex<Mount> mount_at(std::size_t i) override;
    [[nodiscard]] svc::IStorageBackend* backend(std::size_t i) noexcept override;

    [[nodiscard]] std::span<const std::byte> order(std::size_t i) noexcept override;

    [[nodiscard]] proto::IImageSource& descriptor() noexcept override {
        TASTY_SEAT_BODY(SaveChannel);
        return *this;
    }

    [[nodiscard]] proto::SlotAttributes attributes(proto::SlotIndex) const override;

    [[nodiscard]] std::uint8_t request_tag(proto::SlotIndex) const noexcept override;
    void note_attached(proto::SlotIndex, bool exists, proto::FileSize) noexcept override;

private:
    [[nodiscard]] static constexpr std::size_t blank_cap(std::size_t i) noexcept {
        return i == 0 ? kMaxPartitionBytes : kPakBytes;
    }

    infra::OptRef<const ISaveLayout> live_{};
    std::optional<NvramStore> store_{};
    SavePath paths_[kMaxSaveRows]{};
    SavePath stock_path_{};
    std::uint64_t total_ = 0;

    std::optional<std::size_t> pending_plan_{};
    std::uint32_t name_refusals_ = 0;
    std::uint32_t opens_ = 0;
    std::uint32_t open_failures_ = 0;

    SaveOrder orders_[kMaxSaveRows]{};
    std::uint32_t declared_[kMaxSaveRows]{};
    bool byteswap_[kMaxSaveRows]{};
    SaveChannelStore io_;
    SavePort ports_[kMaxSaveRows];
};

}
