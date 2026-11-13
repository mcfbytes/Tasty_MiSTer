// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/error.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "hal/spi_transport.h"
#include "proto/block_backing.h"
#include "proto/block_geometry.h"
#include "proto/block_roles.h"
#include "proto/block_server.h"
#include "proto/image_source.h"
#include "proto/late_answers.h"
#include "proto/slot_attributes.h"
#include "proto/slot_record.h"
#include "proto/spi_block_poll.h"
#include "proto/storage_channel.h"
#include "proto/storage_completion.h"
#include "proto/types.h"

namespace mister::hal {
class DiskLamp;
}

namespace mister::proto {

class BlockSlots {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Wiring {

        hal::ISpiTransport& link;
        infra::OptRef<IImageSource> source{};
        infra::OptRef<const os::IClock> clock{};

        infra::OptRef<IStorageChannel> channel{};

        infra::OptRef<hal::DiskLamp> lamp{};
    };

    explicit BlockSlots(hal::ISpiTransport& link) noexcept;
    explicit BlockSlots(Wiring w) noexcept;

    BlockSlots(const BlockSlots&) = delete;
    BlockSlots& operator=(const BlockSlots&) = delete;

    static constexpr std::int64_t kAnswerDeadlineNs = 250'000'000;
    static constexpr std::int64_t kPrefetchDeadlineNs = 1'000'000'000;
    static constexpr std::int64_t kStagingDeadlineNs = 1'000'000'000;

    static constexpr std::int64_t kLateAnswerNs = 4'000'000;

    using Slot = SlotRecord::Slot;

    struct Diagnostics {
        std::uint32_t unencodable_announce = 0;
        std::uint32_t blank_filled = 0;
        std::uint32_t write_failures = 0;
        std::uint32_t discarded_writes = 0;
        std::uint32_t deferred_passes = 0;

        std::uint32_t blocks_served = 0;
        std::uint32_t answer_expiries = 0;
        std::uint32_t prefetch_expiries = 0;
        std::uint32_t staging_expiries = 0;
        std::uint32_t stale_completions = 0;
        std::uint32_t rearmed_substitutes = 0;
        std::uint32_t late_answers = 0;
        std::uint32_t lw_slot_passes = 0;
    };

    enum class Pass : std::uint8_t { Idle, Answered, Deferred };

    void set_storage_live(bool live) noexcept { storage_live_ = live; }

    void bind_lw_slots(BlockSlotMask mask) noexcept { lw_slots_ = mask; }

    void bind_server(SlotIndex slot, SlotServer server) noexcept {
        if (!slot.below<kBlockSlots>())
            return;
        end_window_(slot);
        server_[slot] = server;
    }

    [[nodiscard]] SlotServer server(SlotIndex slot) const noexcept {
        return slot_server_of(server_, slot);
    }

    [[nodiscard]] Ex<void> mount(
        SlotIndex slot,
        FileSize size_bytes,
        PathId path_id,
        IBlockRoles& roles
    );
    [[nodiscard]] Ex<void> unmount(SlotIndex slot);

    [[nodiscard]] Ex<void> install_disc(SlotIndex slot, FileSize size_bytes, IBlockRoles& roles);

    void change_disc(SlotIndex slot) noexcept;

    void count_unencodable_announce() noexcept { ++diag_.unencodable_announce; }

    [[nodiscard]] Ex<bool> answer_read(
        SlotIndex slot,
        Lba lba,
        std::uint32_t nbytes,
        SectorAck ack
    );

    [[nodiscard]] Ex<Pass> serve(const SpiBlockPoll::Decode& d, IBlockRoles& roles);

    [[nodiscard]] Ex<SpiBlockPoll::Decode> poll_block(
        IBlockRoles& roles,
        std::optional<SdStatusWord> known = std::nullopt
    );

    unsigned install_completions(IBlockRoles& roles) noexcept;

    [[nodiscard]] Ex<void> submit_attach(SlotIndex slot, IBlockRoles& roles) noexcept;
    [[nodiscard]] Ex<void> submit_detach(SlotIndex slot, IBlockRoles& roles) noexcept;
    [[nodiscard]] bool slot_busy(SlotIndex slot) const noexcept;

    const Slot& slot(SlotIndex i) const;

    void invalidate(SlotIndex i);

    void reset_all() noexcept;

    void on(
        const StorageCompletion::Fetch& a,
        const StorageCompletion::Head& h,
        IBlockRoles& roles
    ) noexcept;
    void on(
        const StorageCompletion::ReadAhead& a,
        const StorageCompletion::Head& h,
        IBlockRoles& roles
    ) noexcept;
    void on(
        const StorageCompletion::Write& a,
        const StorageCompletion::Head& h,
        IBlockRoles& roles
    ) noexcept;
    void on(
        const StorageCompletion::Create& a,
        const StorageCompletion::Head& h,
        IBlockRoles& roles
    ) noexcept;
    void on(
        const StorageCompletion::Flush& a,
        const StorageCompletion::Head& h,
        IBlockRoles& roles
    ) noexcept;
    void on(
        const StorageCompletion::Attach& a,
        const StorageCompletion::Head& h,
        IBlockRoles& roles
    ) noexcept;
    void on(
        const StorageCompletion::Detach& a,
        const StorageCompletion::Head& h,
        IBlockRoles& roles
    ) noexcept;
    void misrouted(const StorageCompletion& c, IBlockRoles& roles) noexcept;

    const Diagnostics& diagnostics() const noexcept { return diag_; }

    [[nodiscard]] LateAnswers take_late_answers() noexcept {
        const LateAnswers v{.count = diag_.late_answers, .max_us = late_max_us_};
        late_max_us_ = 0;
        return v;
    }

    SpiBlockPoll& block_poll() noexcept { return block_poll_; }

    [[nodiscard]] const SpiBlockPoll& block_poll() const noexcept { return block_poll_; }

private:
    enum class Gate : std::uint8_t { Open, Defer, Substitute };

    [[nodiscard]] Gate gate_(SlotIndex slot, os::RawMonotonicClock::time_point now) noexcept;

    void expire_(SlotRecord::Credit& c) noexcept;

    void drop_(SlotIndex slot) noexcept;

    void end_window_(SlotIndex slot) noexcept;

    [[nodiscard]] std::span<const std::uint8_t> substitute_(
        SlotIndex slot,
        Lba lba,
        const BlockBacking& backing
    );

    [[nodiscard]] std::optional<StorageSeq> submit_(const StorageRequest& r) noexcept;

    [[nodiscard]] Ex<void> send_config();
    [[nodiscard]] Ex<void> announce(SlotIndex slot, FileSize size_bytes, bool writable);

    struct MountSizes {
        FileSize file_bytes{};
        FileSize size_bytes{};
    };

    void mount_row_(
        SlotIndex slot,
        const SlotAttributes& attr,
        MountSizes sizes,
        PathId path_id
    ) noexcept;

    [[nodiscard]] Ex<Pass> serve_read(
        const SdRequest& req,
        BlockSize block_size,
        SectorAck ack,
        const BlockBacking& backing
    );
    [[nodiscard]] Ex<Pass> serve_write(
        const SdRequest& req,
        BlockSize block_size,
        SectorAck ack,
        const BlockBacking& backing
    );

    struct ReadPass {
        SlotIndex slot;
        Lba lba;
        BlockCount blks;
        BlockSize block_size;
        FileSize nbytes;
        SectorAck ack;
        const BlockBacking& backing;
    };

    [[nodiscard]] Ex<Pass> answer_read_image_(const ReadPass& p, const ResidentWindow::Hit& image);

    [[nodiscard]] Ex<Pass> answer_read_substitute_(const ReadPass& p, bool rearmed);

    [[nodiscard]] Ex<void> emit_read_data_phase_(
        SlotIndex slot,
        std::span<const std::uint8_t> serve,
        std::size_t offset,
        FileSize nbytes,
        SectorAck ack,
        BlockCount blks,
        bool count_blocks
    );

    [[nodiscard]] bool submit_fetch(
        SlotIndex slot,
        Lba base,
        BlockSize block_size,
        bool answer_owed,
        const BlockBacking& backing
    ) noexcept;
    [[nodiscard]] bool submit_create(
        SlotIndex slot,
        std::uint32_t len,
        const BlockBacking& backing
    ) noexcept;
    [[nodiscard]] bool submit_write(
        SlotIndex slot,
        std::uint64_t offset,
        std::uint32_t len,
        const BlockBacking& backing
    ) noexcept;
    [[nodiscard]] Ex<void> submit_lifecycle_(SlotIndex slot, const StorageRequest& r) noexcept;
    [[nodiscard]] StorageRequest::Head request_head_(
        SlotIndex slot,
        const BlockBacking& backing
    ) const noexcept;
    [[nodiscard]] std::optional<SlotIndex> slot_of_(const StorageCompletion::Head& h) noexcept;
    [[nodiscard]] os::RawMonotonicClock::time_point now_at_() const noexcept;
    void invalidate_all() noexcept;

    [[nodiscard]] BlockBacking backing_(IBlockRoles& roles, SlotIndex slot) const noexcept {
        if (!slot.below<kBlockSlots>())
            return {};
        return roles.resolve(slot_server_of(server_, slot));
    }

    [[nodiscard]] infra::OptRef<IImageSource> source_of_(const BlockBacking& b) const noexcept {
        if (const auto src = b.source())
            return src;
        return source_;
    }

    hal::ISpiTransport& link_;

    infra::IndexedArray<SlotRecord, kBlockSlots, SlotIndex> records_{};

    SlotServers server_{};
    std::uint32_t late_max_us_ = 0;
    void note_answer_wait_(std::chrono::nanoseconds waited) noexcept;

    infra::OptRef<IStorageChannel> chan_;
    infra::OptRef<const os::IClock> clock_;
    bool storage_live_ = false;
    BlockSlotMask lw_slots_{};
    StorageSeq next_seq_{1};
    infra::OptRef<IImageSource> source_;

    enum : std::size_t { kBlankPageAlign = 64 };

    alignas(kBlankPageAlign) std::array<std::uint8_t, kBlockStagingBytes> blank_page_{};

    SpiBlockPoll block_poll_{SpiBlockPoll::Wiring{
        .link = link_,
        .slot0_file_bytes = infra::OptRef<const std::uint64_t>{
            records_[SlotIndex{0}].slot.file_bytes.v
        }
    }};
    Diagnostics diag_{};
    infra::OptRef<hal::DiskLamp> lamp_{};
};

}
