// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/counter.h"
#include "infra/loan_channel.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/block_geometry.h"
#include "proto/storage_channel.h"
#include "svc/block_ctl.h"
#include "svc/block_half.h"
#include "svc/io_coworker.h"
#include "svc/save_files_on_io.h"
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
                             public IIoCoworker,
                             public ISaveFilesOnIo {
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
        proto::StorageCompletion answer{};
        infra::OptRef<IStorageBackend> backend{};
        std::uint32_t gen = 0;
        std::span<const std::byte> order{};
        bool performed = false;
    };

    void on(
        const proto::StorageRequest::Read& a,
        const proto::StorageRequest::Head& h,
        Work& w
    ) noexcept;
    void on(
        const proto::StorageRequest::Write& a,
        const proto::StorageRequest::Head& h,
        Work& w
    ) noexcept;
    void on(
        const proto::StorageRequest::Create& a,
        const proto::StorageRequest::Head& h,
        Work& w
    ) noexcept;
    void on(
        const proto::StorageRequest::Flush& a,
        const proto::StorageRequest::Head& h,
        Work& w
    ) noexcept;
    void on(
        const proto::StorageRequest::Attach& a,
        const proto::StorageRequest::Head& h,
        Work& w
    ) noexcept;
    void on(
        const proto::StorageRequest::Detach& a,
        const proto::StorageRequest::Head& h,
        Work& w
    ) noexcept;
    void misrouted(const proto::StorageRequest& r, Work& w) noexcept;

    void serve() noexcept override;
    [[nodiscard]] bool idle() const noexcept override;

    [[nodiscard]] ISaveFilesOnIo::Save save_on_io(proto::SlotIndex slot) const noexcept override;

    [[nodiscard]] StorageCounters counters() const noexcept;

    [[nodiscard]] std::uint32_t abandoned() const noexcept { return abandoned_.get(); }

    [[nodiscard]] Data& data_channel_for_test() noexcept { return data_; }

    [[nodiscard]] proto::StorageSeq submit(const proto::StorageRequest&) noexcept override;
    [[nodiscard]] std::optional<proto::StorageCompletion> reap() noexcept override;
    [[nodiscard]] std::span<const std::uint8_t> half(
        proto::SlotIndex,
        proto::ArenaHalf
    ) const noexcept override;
    [[nodiscard]] std::span<std::uint8_t> half_mut(
        proto::SlotIndex,
        proto::ArenaHalf
    ) noexcept override;

    [[nodiscard]] bool stage_backend(
        proto::SlotIndex,
        IStorageBackend&,
        std::span<const std::byte> order
    ) noexcept override;
    [[nodiscard]] bool release_slot(proto::SlotIndex) noexcept override;
    [[nodiscard]] bool quiesced() const noexcept override;

private:
    void own_all_() noexcept;

    static void set_order_(BlockCtl& hand, std::span<const std::byte> order) noexcept;

    [[nodiscard]] bool home_(unsigned slot) const noexcept;

    void perform_(const proto::StorageRequest& ask, Work& w) noexcept;

    [[nodiscard]] infra::OptRef<IStorageBackend> live_(
        proto::SlotIndex slot,
        const Work& w
    ) const noexcept;

    void finish_(proto::StorageStatus st, Work& w, const proto::StorageCompletion& c) noexcept;
    Data data_;
    Ctl ctl_;

    struct Rt {

        Data::Loan data_hands[kSlots][kHalves]{};
        Ctl::Loan ctl_hands[kSlots]{};

        infra::OptRef<IStorageBackend> bound[kSlots]{};

        std::uint32_t gen[kSlots]{};
        bool owned = false;
    };

    Rt rt_{};

    struct Io {
        std::uint32_t served[kSlots]{};
        ISaveFilesOnIo::Save saves[kSlots]{};
    };

    Io io_{};

    xthread::Counter submits_, completions_, errors_, refusals_, depth_max_, request_misrouted_;
    xthread::Counter abandoned_{};
};

static_assert(
    sizeof(StorageService) > 512u * 1024u,
    "the arena dominates; a later pass that quadruples the halves argues for it at "
    "the point of change"
);

}
