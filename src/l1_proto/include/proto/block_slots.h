// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "hal/spi_transport.h"
#include "proto/block_geometry.h"
#include "proto/block_geometry_hook.h"
#include "proto/image_source.h"
#include "proto/late_answers.h"
#include "proto/resident_image_source.h"
#include "proto/slot_record.h"
#include "proto/slot_roles.h"
#include "proto/spi_block_poll.h"
#include "proto/storage_channel.h"
#include "proto/storage_completion.h"
#include "proto/types.h"

namespace mister::proto {

class BlockSlots {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Wiring {
        infra::OptRef<IImageSource> source{};
        infra::OptRef<const os::IClock> clock{};

        infra::OptRef<IStorageChannel> channel{};
    };

    BlockSlots() noexcept;
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
    };

    enum class Pass : std::uint8_t { Idle, Answered, Deferred };

    void set_storage_live(bool live) noexcept { storage_live_ = live; }

    void bind_roles(SlotIndex slot, const SlotRoles& roles) noexcept {
        if (slot.v >= kBlockSlots)
            return;
        records_[slot.v].window = Window{};
        roles_[slot.v] = roles;
    }

    [[nodiscard]] Ex<void> mount(
        hal::ISpiTransport& link,
        SlotIndex slot,
        FileSize size_bytes,
        PathId path_id
    );
    [[nodiscard]] Ex<void> unmount(hal::ISpiTransport& link, SlotIndex slot);

    [[nodiscard]] Ex<void> install_disc(SlotIndex slot, FileSize size_bytes);

    void count_unencodable_announce() noexcept { ++diag_.unencodable_announce; }

    [[nodiscard]] Ex<bool> answer_read(
        hal::ISpiTransport& link,
        SlotIndex slot,
        Lba lba,
        std::uint32_t nbytes,
        std::uint16_t ack
    );

    [[nodiscard]] Ex<Pass> serve(hal::ISpiTransport& link, const SpiBlockPoll::Decode& d);

    unsigned install_completions() noexcept;

    [[nodiscard]] bool submit_attach(SlotIndex slot) noexcept;
    [[nodiscard]] bool submit_detach(SlotIndex slot) noexcept;

    [[nodiscard]] bool slot_busy(SlotIndex slot) const noexcept {
        return slot.v < kBlockSlots && records_[slot.v].gate.state != SlotGate::State::Idle;
    }

    const Slot& slot(SlotIndex i) const;

    void invalidate(SlotIndex i);

    void reset_all() noexcept {
        for (unsigned i = 0; i < kBlockSlots; ++i) {
            records_[i].slot = Slot{};
            roles_[i] = SlotRoles{};
            records_[i].gate = SlotGate{};
        }
        invalidate_all();
    }

    void on(
        const StorageCompletion::Read& a,
        const StorageCompletion::Head& h,
        SlotIndex slot
    ) noexcept;
    void on(
        const StorageCompletion::Write& a,
        const StorageCompletion::Head& h,
        SlotIndex slot
    ) noexcept;
    void on(
        const StorageCompletion::Create& a,
        const StorageCompletion::Head& h,
        SlotIndex slot
    ) noexcept;
    void on(
        const StorageCompletion::Flush& a,
        const StorageCompletion::Head& h,
        SlotIndex slot
    ) noexcept;
    void on(
        const StorageCompletion::Attach& a,
        const StorageCompletion::Head& h,
        SlotIndex slot
    ) noexcept;
    void on(
        const StorageCompletion::Detach& a,
        const StorageCompletion::Head& h,
        SlotIndex slot
    ) noexcept;
    void misrouted(const StorageCompletion& c, SlotIndex slot) noexcept;

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

    [[nodiscard]] Ex<void> send_config(hal::ISpiTransport& link);
    [[nodiscard]] Ex<void> announce(
        hal::ISpiTransport& link,
        SlotIndex slot,
        FileSize size_bytes,
        bool writable
    );

    [[nodiscard]] Ex<Pass> serve_read(
        hal::ISpiTransport& link,
        const SdRequest& req,
        std::uint32_t block_size,
        std::uint32_t window_blocks,
        std::uint16_t ack,
        IResidentImageSource* res
    );
    [[nodiscard]] Ex<Pass> serve_write(
        hal::ISpiTransport& link,
        const SdRequest& req,
        std::uint32_t block_size,
        std::uint16_t ack,
        IResidentImageSource* res
    );

    [[nodiscard]] Ex<void> emit_read_data_phase_(
        hal::ISpiTransport& link,
        SlotIndex slot,
        std::span<const std::uint8_t> serve,
        std::uint32_t offset,
        std::uint32_t nbytes,
        std::uint16_t ack,
        std::uint32_t blks,
        bool count_blocks
    );
    static constexpr std::uint8_t kNoHalf = SlotRecord::kNoHalf;

    enum class Refill : std::uint8_t { Hit, Miss, Defer };
    Refill refill_resident(
        SlotIndex slot,
        Lba base,
        std::uint32_t block_size,
        IResidentImageSource& src,
        bool answer_owed
    );

    [[nodiscard]] bool submit_fill(
        SlotIndex slot,
        Lba base,
        std::uint32_t block_size,
        bool answer_owed
    ) noexcept;

    [[nodiscard]] bool submit_create(SlotIndex slot, ArenaHalf half, std::uint32_t len) noexcept;
    [[nodiscard]] bool submit_write(
        SlotIndex slot,
        ArenaHalf half,
        std::uint64_t offset,
        std::uint32_t len
    ) noexcept;

    void gate_commit_(
        SlotIndex slot,
        StorageSeq seq,
        ArenaHalf half,
        std::uint64_t offset,
        std::uint32_t len
    ) noexcept;
    [[nodiscard]] bool submit_lifecycle_(SlotIndex slot, const StorageRequest& r) noexcept;
    [[nodiscard]] StorageRequest::Head request_head_(SlotIndex slot) const noexcept;
    void apply_completion(SlotIndex slot, const StorageCompletion& c) noexcept;

    [[nodiscard]] bool stale_(SlotIndex slot, const StorageCompletion::Head& h) noexcept;
    void expire(SlotIndex slot) noexcept;
    [[nodiscard]] std::int64_t now_ns() const noexcept;

    enum class Blank : std::uint8_t { Filled, NoBuffer, Deferred, Static };
    [[nodiscard]] Blank substitute_blank(SlotIndex slot, Lba lba);
    [[nodiscard]] Blank no_half_answer(SlotIndex slot) noexcept;

    void invalidate_all() noexcept {
        for (unsigned i = 0; i < kBlockSlots; ++i)
            records_[i].drop_window();
    }

    IImageSource* source_for(SlotIndex s) const noexcept {
        const SlotRoles& r = roles_[s.v];
        if (r.descriptor)
            return &*r.descriptor;
        if (r.resident)
            return &*r.resident;
        return source_ ? &*source_ : nullptr;
    }

    IResidentImageSource* resident_for(SlotIndex s) const noexcept {
        return roles_[s.v].resident ? &*roles_[s.v].resident : nullptr;
    }

    SlotRecord records_[kBlockSlots]{};
    std::uint32_t late_max_us_ = 0;
    void note_answer_wait_(std::int64_t waited_ns) noexcept;

    infra::OptRef<IStorageChannel> chan_;
    infra::OptRef<const os::IClock> clock_;
    bool storage_live_ = false;
    std::uint32_t next_seq_ = 1;
    infra::OptRef<IImageSource> source_;
    SlotRoleTable roles_{};

    SpiBlockPoll block_poll_{SpiBlockPoll::Wiring{
        .slot0_file_bytes = infra::OptRef<const std::uint64_t>{records_[0].slot.file_bytes.v},
        .roles = infra::OptRef<const SlotRoleTable>{roles_}
    }};
    Diagnostics diag_{};
};

}
