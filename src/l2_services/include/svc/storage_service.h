// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "infra/counter.h"
#include "infra/indexed_array.h"
#include "infra/loan_channel.h"
#include "infra/opt_ref.h"
#include "infra/polled.h"
#include "infra/pool_slot.h"
#include "infra/seat.h"
#include "proto/block_geometry.h"
#include "proto/storage_channel.h"
#include "svc/block_ctl.h"
#include "svc/io_coworker.h"
#include "svc/save_files_on_io.h"
#include "svc/slot_arena.h"
#include "svc/storage_backend.h"
#include "svc/storage_lifecycle.h"

namespace mister::svc {

struct StorageCounters {
    std::uint32_t submits = 0;
    std::uint32_t completions = 0;
    std::uint32_t errors = 0;
    std::uint32_t refusals = 0;
    std::uint32_t depth_max = 0;
    std::uint32_t misrouted = 0;
};

class StorageService final : public proto::IStorageChannel,
                             public IStorageLifecycle,
                             public IIoCoworker,
                             public ISaveFilesOnIo {
    TASTY_SEAT_MEDIATOR(Io, RT);

public:
    static constexpr unsigned kSlots = proto::kBlockSlots;
    static constexpr std::uint32_t kArenaBytes = proto::kBlockStagingBytes;

    using Data = xthread::LoanChannel<SlotArena, kSlots, SeatTag::RT, SeatTag::Io>;
    using Ctl = xthread::LoanChannel<BlockCtl, kSlots, SeatTag::RT, SeatTag::Io>;

    StorageService() noexcept : data_(xthread::Polled{}), ctl_(xthread::Polled{}) {}

    explicit StorageService(xthread::WakeFlag& io_wake) noexcept : data_(io_wake), ctl_(io_wake) {}

    ~StorageService() override;

    struct Work {
        infra::OptRef<SlotArena> arena{};
        proto::StorageCompletion answer{};
        infra::OptRef<IStorageBackend> backend{};
        Generation gen{};

        infra::OptRef<const BlockCtl> hand{};

        infra::OptRef<const BlankSaveSpec> blank{};
        bool performed = false;
    };

    void on(
        const proto::StorageRequest::Fetch& a,
        const proto::StorageRequest::Head& h,
        Work& w
    ) noexcept;
    void on(
        const proto::StorageRequest::ReadAhead& a,
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

    [[nodiscard]] std::optional<proto::StorageSeq> submit(
        const proto::StorageRequest&
    ) noexcept override;
    [[nodiscard]] std::optional<proto::StorageCompletion> reap() noexcept override;
    [[nodiscard]] HandsOut hands_out() const noexcept override;
    [[nodiscard]] std::optional<HomeWindow> window(proto::SlotIndex) const noexcept override;
    [[nodiscard]] std::span<std::uint8_t> claim(proto::SlotIndex) noexcept override;
    void install(proto::SlotIndex, const proto::ResidentWindow&) noexcept override;

    [[nodiscard]] bool stage_backend(
        proto::SlotIndex,
        IStorageBackend&,
        std::span<const std::byte> order,
        infra::OptRef<const BlankSaveSpec> blank
    ) noexcept override;
    [[nodiscard]] bool release_slot(proto::SlotIndex) noexcept override;
    [[nodiscard]] bool quiesced() const noexcept override;

private:
    void own_all_() noexcept;

    static void set_order_(
        BlockCtl& hand,
        std::span<const std::byte> order,
        infra::OptRef<const BlankSaveSpec> blank
    ) noexcept;

    [[nodiscard]] bool home_(proto::SlotIndex slot) const noexcept;

    void perform_(const proto::StorageRequest& ask, Work& w) noexcept;

    [[nodiscard]] infra::OptRef<IStorageBackend> live_(
        proto::SlotIndex slot,
        const Work& w
    ) const noexcept;

    void finish_(proto::StorageStatus st, Work& w, const proto::StorageCompletion& c) noexcept;
    Data data_;
    Ctl ctl_;

    struct Rt {

        class Slot {
            TASTY_SEAT_RESIDENT(RT);

        public:
            [[nodiscard]] Data::Loan& hand() noexcept { return data_hand_; }

            [[nodiscard]] const Data::Loan& hand() const noexcept { return data_hand_; }

            [[nodiscard]] Ctl::Loan& ctl() noexcept { return ctl_hand_; }

            [[nodiscard]] const Ctl::Loan& ctl() const noexcept { return ctl_hand_; }

            [[nodiscard]] bool home() const noexcept { return data_hand_ && ctl_hand_; }

            void drop_hands() noexcept {
                data_hand_ = Data::Loan{};
                ctl_hand_ = Ctl::Loan{};
            }

            [[nodiscard]] infra::OptRef<IStorageBackend> bound() const noexcept { return bound_; }

            [[nodiscard]] Generation gen() const noexcept { return gen_; }

            void bind(infra::OptRef<IStorageBackend> stage) noexcept {
                bound_ = stage;
                gen_ = gen_.next();
            }

            void unbind() noexcept {
                bound_.reset();
                gen_ = gen_.next();
            }

            void forget() noexcept { bound_.reset(); }

        private:
            Data::Loan data_hand_{};
            Ctl::Loan ctl_hand_{};
            infra::OptRef<IStorageBackend> bound_{};
            Generation gen_{};
        };

        infra::IndexedArray<Slot, kSlots, proto::SlotIndex> slots{};
        bool owned = false;
    };

    Rt rt_{};

    struct Io {
        class Slot {
            TASTY_SEAT_RESIDENT(Io);

        public:
            [[nodiscard]] bool serves(Generation gen) const noexcept { return gen == served_; }

            void serve(Generation gen) noexcept { served_ = gen; }

            void attached(
                infra::OptRef<IStorageBackend> file,
                std::optional<proto::FileSize> extent,
                bool growable,
                infra::OptRef<const BlankSaveSpec> blank
            ) noexcept {
                save_ = ISaveFilesOnIo::Save{
                    .file = file,
                    .extent = extent,
                    .growable = growable,
                    .gen = save_.gen.next(),
                    .blank = blank
                };
            }

            void detached() noexcept { save_ = ISaveFilesOnIo::Save{.gen = save_.gen.next()}; }

            [[nodiscard]] const ISaveFilesOnIo::Save& save() const noexcept { return save_; }

        private:
            Generation served_{};
            ISaveFilesOnIo::Save save_{};
        };

        infra::IndexedArray<Slot, kSlots, proto::SlotIndex> slots{};
    };

    Io io_{};

    xthread::Counter submits_, completions_, errors_, refusals_, depth_max_, request_misrouted_;
    xthread::Counter abandoned_{};
};

static_assert(sizeof(StorageService) > 256u * 1024u, "one window per slot dominates");

}
