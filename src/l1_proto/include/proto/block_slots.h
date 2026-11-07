// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
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
#include "proto/slot_record.h"
#include "proto/spi_block_poll.h"
#include "proto/storage_channel.h"
#include "proto/storage_completion.h"
#include "proto/types.h"

namespace mister::proto {

class BlockSlots {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Wiring {

        hal::ISpiTransport& link;
        infra::OptRef<IImageSource> source{};
        infra::OptRef<const os::IClock> clock{};

        infra::OptRef<IStorageChannel> channel{};
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
        std::uint32_t no_half_substitutes = 0;
        std::uint32_t late_answers = 0;
        std::uint32_t lw_slot_passes = 0;
    };

    enum class Pass : std::uint8_t { Idle, Answered, Deferred };

    void set_storage_live(bool live) noexcept { storage_live_ = live; }

    void bind_lw_slots(BlockSlotMask mask) noexcept { lw_slots_ = mask; }

    void bind_server(SlotIndex slot, SlotServer server) noexcept {
        if (!slot.below<kBlockSlots>())
            return;
        records_[slot].window = Window{};
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

    [[nodiscard]] bool submit_attach(SlotIndex slot, IBlockRoles& roles) noexcept;
    [[nodiscard]] bool submit_detach(SlotIndex slot, IBlockRoles& roles) noexcept;

    [[nodiscard]] bool slot_busy(SlotIndex slot) const noexcept {
        return slot.below<kBlockSlots>() && records_[slot].gate.state != SlotGate::State::Idle;
    }

    const Slot& slot(SlotIndex i) const;

    void invalidate(SlotIndex i);

    void reset_all() noexcept {
        for (SlotRecord& r : records_) {
            r.slot = Slot{};
            r.gate = SlotGate{};
        }
        for (SlotServer& sv : server_)
            sv = SlotServer::Session;
        lw_slots_ = BlockSlotMask{};
        invalidate_all();
    }

    void on(
        const StorageCompletion::Read& a,
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
    using Window = SlotRecord::Window;
    using SlotGate = SlotRecord::SlotGate;

    [[nodiscard]] Ex<void> send_config();
    [[nodiscard]] Ex<void> announce(SlotIndex slot, FileSize size_bytes, bool writable);

    [[nodiscard]] Ex<Pass> serve_read(
        const SdRequest& req,
        std::uint32_t block_size,
        std::uint32_t window_blocks,
        SectorAck ack,
        const BlockBacking& backing
    );
    [[nodiscard]] Ex<Pass> serve_write(
        const SdRequest& req,
        std::uint32_t block_size,
        SectorAck ack,
        const BlockBacking& backing
    );

    [[nodiscard]] Ex<void> emit_read_data_phase_(
        SlotIndex slot,
        std::span<const std::uint8_t> serve,
        std::uint32_t offset,
        std::uint32_t nbytes,
        SectorAck ack,
        std::uint32_t blks,
        bool count_blocks
    );

    enum class Refill : std::uint8_t { Hit, Miss, Defer };
    Refill refill_resident(
        SlotIndex slot,
        Lba base,
        std::uint32_t block_size,
        IBlockServer& src,
        bool answer_owed
    );

    [[nodiscard]] bool submit_fill(
        SlotIndex slot,
        Lba base,
        std::uint32_t block_size,
        bool answer_owed,
        const BlockBacking& backing
    ) noexcept;

    [[nodiscard]] bool submit_create(
        SlotIndex slot,
        ArenaHalf half,
        std::uint32_t len,
        const BlockBacking& backing
    ) noexcept;
    [[nodiscard]] bool submit_write(
        SlotIndex slot,
        ArenaHalf half,
        std::uint64_t offset,
        std::uint32_t len,
        const BlockBacking& backing
    ) noexcept;

    void gate_commit_(
        SlotIndex slot,
        StorageSeq seq,
        ArenaHalf half,
        std::uint64_t offset,
        std::uint32_t len
    ) noexcept;
    [[nodiscard]] bool submit_lifecycle_(SlotIndex slot, const StorageRequest& r) noexcept;
    [[nodiscard]] StorageRequest::Head request_head_(
        SlotIndex slot,
        const BlockBacking& backing
    ) const noexcept;
    [[nodiscard]] std::optional<SlotIndex> slot_of_(const StorageCompletion::Head& h) noexcept;

    [[nodiscard]] bool stale_(SlotIndex slot, const StorageCompletion::Head& h) noexcept;
    void expire(SlotIndex slot) noexcept;
    [[nodiscard]] std::int64_t now_ns() const noexcept;

    enum class Blank : std::uint8_t { Filled, NoBuffer, Deferred, Static };
    [[nodiscard]] Blank substitute_blank(SlotIndex slot, Lba lba, const BlockBacking& backing);
    [[nodiscard]] Blank no_half_answer(SlotIndex slot) noexcept;

    void invalidate_all() noexcept {
        for (SlotRecord& r : records_)
            r.drop_window();
    }

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
    void note_answer_wait_(std::int64_t waited_ns) noexcept;

    infra::OptRef<IStorageChannel> chan_;
    infra::OptRef<const os::IClock> clock_;
    bool storage_live_ = false;
    BlockSlotMask lw_slots_{};
    std::uint32_t next_seq_ = 1;
    infra::OptRef<IImageSource> source_;

    SpiBlockPoll block_poll_{SpiBlockPoll::Wiring{
        .link = link_,
        .slot0_file_bytes = infra::OptRef<const std::uint64_t>{
            records_[SlotIndex{0}].slot.file_bytes.v
        }
    }};
    Diagnostics diag_{};
};

}
