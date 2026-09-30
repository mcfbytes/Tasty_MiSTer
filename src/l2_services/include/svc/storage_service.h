// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/loan_channel.h"
#include "infra/seat.h"
#include "proto/block_geometry.h"
#include "proto/storage_channel.h"
#include "svc/block_ctl.h"
#include "svc/block_half.h"
#include "svc/io_coworker.h"
#include "svc/storage_backend.h"
#include "svc/storage_lifecycle.h"

namespace mister::svc {

struct StorageCounters {
    std::uint32_t submits = 0;
    std::uint32_t completions = 0;
    std::uint32_t errors = 0;
    std::uint32_t refusals = 0;
    std::uint32_t depth_max = 0;
    std::uint32_t state = 0;
    std::uint32_t misrouted = 0;
};

class StorageService final : public proto::IStorageChannel,
                             public IStorageLifecycle,
                             public IIoCoworker {
    TASTY_SEAT_MEDIATOR(Io, RT);

public:
    static constexpr unsigned kSlots = proto::kBlockSlots;
    static constexpr unsigned kHalves = 2;
    static constexpr std::uint32_t kHalfBytes = proto::kBlockStagingBytes;

    using Data = xthread::LoanChannel<BlockHalf, kSlots * kHalves, SeatTag::RT, SeatTag::Io>;
    using Ctl = xthread::LoanChannel<BlockCtl, kSlots, SeatTag::RT, SeatTag::Io>;

    StorageService() noexcept : data_(xthread::Polled{}), ctl_(xthread::Polled{}) {}

    explicit StorageService(xthread::WakeFlag& io_wake) noexcept : data_(io_wake), ctl_(io_wake) {}
    ~StorageService() override;

    struct Work {
        std::span<std::uint8_t> data;
        proto::StorageCompletion& answer;
        bool performed = false;
    };

    void on(const proto::StorageRequest::Read& a, const proto::StorageRequest::Head& h,
            Work& w) noexcept;
    void on(const proto::StorageRequest::Write& a, const proto::StorageRequest::Head& h,
            Work& w) noexcept;
    void on(const proto::StorageRequest::Create& a, const proto::StorageRequest::Head& h,
            Work& w) noexcept;
    void on(const proto::StorageRequest::Flush& a, const proto::StorageRequest::Head& h,
            Work& w) noexcept;
    void on(const proto::StorageRequest::Attach& a, const proto::StorageRequest::Head& h,
            Work& w) noexcept;
    void on(const proto::StorageRequest::Detach& a, const proto::StorageRequest::Head& h,
            Work& w) noexcept;
    void misrouted(const proto::StorageRequest& r, Work& w) noexcept;

    void serve() noexcept override;
    [[nodiscard]] bool idle() const noexcept override;

    [[nodiscard]] StorageCounters counters() const noexcept;

    [[nodiscard]] std::uint32_t abandoned() const noexcept {
        return abandoned_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] Data& data_channel_for_test() noexcept { return data_; }

    [[nodiscard]] proto::StorageSeq submit(const proto::StorageRequest&) noexcept override;
    [[nodiscard]] std::optional<proto::StorageCompletion> reap() noexcept override;
    [[nodiscard]] std::span<const std::uint8_t> half(proto::SlotIndex,
                                                     proto::ArenaHalf) const noexcept override;
    [[nodiscard]] std::span<std::uint8_t> half_mut(proto::SlotIndex,
                                                   proto::ArenaHalf) noexcept override;

    [[nodiscard]] bool stage_backend(proto::SlotIndex, IStorageBackend&) noexcept override;
    [[nodiscard]] bool release_slot(proto::SlotIndex) noexcept override;
    [[nodiscard]] bool quiesced() const noexcept override;

private:
    void own_all_() noexcept;

    [[nodiscard]] bool home_(unsigned slot) const noexcept;

    void perform_(const proto::StorageRequest& ask, Work& w) noexcept;

    void finish_(proto::StorageStatus st, Work& w, const proto::StorageCompletion& c) noexcept;

    [[nodiscard]] IStorageBackend* data_backend_(proto::SlotIndex slot) noexcept;

    Data data_;
    Ctl ctl_;

    Data::Loan data_hands_[kSlots][kHalves]{};
    Ctl::Loan ctl_hands_[kSlots]{};
    bool owned_ = false;

    std::atomic<std::uint32_t> submits_{0};
    std::atomic<std::uint32_t> completions_{0};
    std::atomic<std::uint32_t> errors_{0};
    std::atomic<std::uint32_t> refusals_{0};
    std::atomic<std::uint32_t> depth_max_{0};
    std::atomic<std::uint32_t> request_misrouted_{0};
    std::atomic<std::uint32_t> abandoned_{0};

    IStorageBackend* attach_[kSlots]{};
    IStorageBackend* backends_[kSlots]{};
};

static_assert(sizeof(StorageService) > 512u * 1024u,
              "the arena dominates; a later pass that quadruples the halves argues for it at "
              "the point of change");

}  // namespace mister::svc
