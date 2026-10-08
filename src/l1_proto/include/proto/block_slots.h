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

    struct Slot {
        enum class State : std::uint8_t { Empty, Mounted };
        State state = State::Empty;
        std::uint32_t block_size = 512;
        FileSize size_bytes{};

        FileSize file_bytes{};
        PathId path_id{};

        bool writable = false;
        bool growable = false;
        bool deferred_create = false;
    };

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

    void attach_source(IImageSource& source) {
        invalidate_all();
        source_ = infra::OptRef<IImageSource>{source};
    }

    void set_storage_live(bool live) noexcept { storage_live_ = live; }

    void bind_roles(SlotIndex slot, const SlotRoles& roles) noexcept {
        if (slot.v >= kBlockSlots) return;
        windows_[slot.v] = Window{};
        roles_[slot.v] = roles;
    }

    Ex<void> mount(hal::ISpiTransport& link, SlotIndex slot, FileSize size_bytes, PathId path_id);
    Ex<void> unmount(hal::ISpiTransport& link, SlotIndex slot);

    [[nodiscard]] Ex<void> mount_cd(hal::ISpiTransport& link, SlotIndex slot, FileSize size_bytes);

    [[nodiscard]] Ex<void> notify_mount(hal::ISpiTransport& link, bool loaded);

    [[nodiscard]] Ex<bool> answer_read(hal::ISpiTransport& link, SlotIndex slot, Lba lba,
                                       std::uint32_t nbytes, std::uint16_t ack);

    [[nodiscard]] Ex<Pass> serve(hal::ISpiTransport& link, const SpiBlockPoll::Decode& d);

    unsigned install_completions() noexcept;

    [[nodiscard]] bool submit_attach(SlotIndex slot) noexcept;
    [[nodiscard]] bool submit_detach(SlotIndex slot) noexcept;
    [[nodiscard]] bool slot_busy(SlotIndex slot) const noexcept {
        return slot.v < kBlockSlots && slot_gate_[slot.v].state != SlotGate::State::Idle;
    }

    const Slot& slot(SlotIndex i) const;

    void invalidate(SlotIndex i);

    void reset_all() noexcept {
        for (unsigned i = 0; i < kBlockSlots; ++i) {
            slots_[i] = Slot{};
            roles_[i] = SlotRoles{};
            slot_gate_[i] = SlotGate{};
        }
        invalidate_all();
    }

    void on(const StorageCompletion::Read& a, const StorageCompletion::Head& h,
            SlotIndex slot) noexcept;
    void on(const StorageCompletion::Write& a, const StorageCompletion::Head& h,
            SlotIndex slot) noexcept;
    void on(const StorageCompletion::Create& a, const StorageCompletion::Head& h,
            SlotIndex slot) noexcept;
    void on(const StorageCompletion::Flush& a, const StorageCompletion::Head& h,
            SlotIndex slot) noexcept;
    void on(const StorageCompletion::Attach& a, const StorageCompletion::Head& h,
            SlotIndex slot) noexcept;
    void on(const StorageCompletion::Detach& a, const StorageCompletion::Head& h,
            SlotIndex slot) noexcept;
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
    struct Window {
        Lba base{};
        std::uint32_t valid_bytes = 0;
        std::uint32_t block_size = 0;
        ArenaHalf half{};
    };

    struct SlotGate {
        enum class State : std::uint8_t { Idle, Fetching, Committing, Staging };
        State state = State::Idle;
        StorageSeq seq{};
        std::uint32_t epoch = 0;
        Lba base{};
        std::uint32_t block_size = 0;
        std::uint64_t offset = 0;
        std::uint32_t len = 0;
        std::int64_t due_ns = 0;
        bool answer_owed = false;
        bool blank_next = false;
    };

    Ex<void> send_config(hal::ISpiTransport& link);
    Ex<void> announce(hal::ISpiTransport& link, SlotIndex slot, FileSize size_bytes, bool writable);
    Ex<Pass> serve_read(hal::ISpiTransport& link, const SdRequest& req, std::uint32_t block_size,
                        std::uint32_t window_blocks, std::uint16_t ack);
    Ex<Pass> serve_write(hal::ISpiTransport& link, const SdRequest& req, std::uint32_t block_size,
                         std::uint16_t ack);

    [[nodiscard]] Ex<void> emit_read_data_phase_(hal::ISpiTransport& link, SlotIndex slot,
                                                 std::span<const std::uint8_t> serve,
                                                 std::uint32_t offset, std::uint32_t nbytes,
                                                 std::uint16_t ack, std::uint32_t blks,
                                                 bool count_blocks);

    [[nodiscard]] ArenaHalf fill_half(SlotIndex slot) const noexcept;
    static constexpr std::uint8_t kNoHalf = 2;
    [[nodiscard]] bool half_busy(SlotIndex slot, ArenaHalf h) const noexcept {
        return (half_busy_[slot.v] & static_cast<std::uint8_t>(1u << h.v)) != 0;
    }

    void drop_window(SlotIndex slot) noexcept {
        windows_[slot.v] = Window{};
        slot_gate_[slot.v].blank_next = false;
        resident_due_ns_[slot.v] = 0;
        ++epoch_[slot.v];
    }

    enum class Refill : std::uint8_t { Hit, Miss, Defer };
    Refill refill_resident(SlotIndex slot, Lba base, std::uint32_t block_size,
                           IResidentImageSource& src, bool answer_owed);

    [[nodiscard]] bool submit_fill(SlotIndex slot, Lba base, std::uint32_t block_size,
                                   bool answer_owed) noexcept;

    [[nodiscard]] bool submit_create(SlotIndex slot, ArenaHalf half, std::uint32_t len) noexcept;
    [[nodiscard]] bool submit_write(SlotIndex slot, ArenaHalf half, std::uint64_t offset,
                                    std::uint32_t len) noexcept;

    void gate_commit_(SlotIndex slot, StorageSeq seq, ArenaHalf half, std::uint64_t offset,
                      std::uint32_t len) noexcept;
    [[nodiscard]] bool submit_lifecycle_(SlotIndex slot, const StorageRequest& r) noexcept;
    [[nodiscard]] StorageRequest::Head request_head_(SlotIndex slot) const noexcept;
    void apply_completion(SlotIndex slot, const StorageCompletion& c) noexcept;

    [[nodiscard]] bool stale_(SlotIndex slot, const StorageCompletion::Head& h) noexcept;
    void finish_gate_(SlotIndex slot, bool blank_next) noexcept {
        slot_gate_[slot.v] = SlotGate{};
        slot_gate_[slot.v].blank_next = blank_next;
    }
    void expire(SlotIndex slot) noexcept;
    [[nodiscard]] std::int64_t now_ns() const noexcept;

    enum class Blank : std::uint8_t { Filled, NoBuffer, Deferred, Static };
    [[nodiscard]] Blank substitute_blank(SlotIndex slot, Lba lba);
    [[nodiscard]] Blank no_half_answer(SlotIndex slot) noexcept;
    void invalidate_all() noexcept {
        for (unsigned i = 0; i < kBlockSlots; ++i)
            drop_window(SlotIndex{static_cast<std::uint8_t>(i)});
    }

    IImageSource* source_for(SlotIndex s) const noexcept {
        const SlotRoles& r = roles_[s.v];
        if (r.descriptor) return &*r.descriptor;
        if (r.resident) return &*r.resident;
        return source_ ? &*source_ : nullptr;
    }
    IResidentImageSource* resident_for(SlotIndex s) const noexcept {
        return roles_[s.v].resident ? &*roles_[s.v].resident : nullptr;
    }

    Slot slots_[kBlockSlots]{};
    Window windows_[kBlockSlots]{};
    SlotGate slot_gate_[kBlockSlots]{};

    std::uint8_t half_busy_[kBlockSlots]{};
    std::uint32_t epoch_[kBlockSlots]{};

    std::int64_t no_half_due_ns_[kBlockSlots]{};

    std::int64_t resident_due_ns_[kBlockSlots]{};
    std::uint32_t late_max_us_ = 0;
    void note_answer_wait_(std::int64_t waited_ns) noexcept;

    IStorageChannel* chan_ = nullptr;
    infra::OptRef<const os::IClock> clock_;
    bool storage_live_ = false;
    std::uint32_t next_seq_ = 1;
    infra::OptRef<IImageSource> source_;
    SlotRoleTable roles_{};

    SpiBlockPoll block_poll_{SpiBlockPoll::Wiring{
        .slot0_file_bytes = infra::OptRef<const std::uint64_t>{slots_[0].file_bytes.v},
        .roles = infra::OptRef<const SlotRoleTable>{roles_}}};
    Diagnostics diag_{};
};

}  // namespace mister::proto
