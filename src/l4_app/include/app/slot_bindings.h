// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "app/board_ops.h"
#include "app/cheat_apply.h"
#include "app/core_block_roles.h"
#include "app/core_option_cells.h"
#include "app/event.h"
#include "app/interned_file.h"
#include "app/link_binder.h"
#include "app/link_encoder.h"
#include "app/link_op_ctx.h"
#include "app/link_op_dispatch.h"
#include "app/link_rx_channel.h"
#include "app/link_tx_reader.h"
#include "app/log_clock.h"
#include "app/mount_status_cell.h"
#include "app/owed_mount.h"
#include "app/owed_save_bind.h"
#include "app/rt_relax_scope.h"
#include "app/save_channel_roles.h"
#include "app/save_extent_cell.h"
#include "app/save_image_source.h"
#include "app/session_bindings.h"
#include "app/session_machine.h"
#include "app/session_tally.h"
#include "app/types.h"
#include "cores/core_support.h"
#include "cores/mounted_path.h"
#include "cores/save_channel.h"
#include "cores/slot_mount.h"
#include "cores/staging_core.h"
#include "hal/spi_transport.h"
#include "infra/error.h"
#include "infra/log_lane.h"
#include "infra/log_rec.h"
#include "infra/message_sum.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/clock.h"
#include "os/deadline.h"
#include "proto/block_slots.h"
#include "proto/disc_announce.h"
#include "proto/download_session.h"
#include "proto/fixed_block_roles.h"
#include "proto/image_bracket.h"
#include "proto/link_op.h"
#include "proto/no_block_roles.h"
#include "proto/spi_frame.h"
#include "proto/storage_channel.h"
#include "proto/types.h"
#include "svc/chd_prefetch.h"
#include "svc/storage_lifecycle.h"
#include "svc/vfs.h"

namespace mister::app {

class SlotBindings {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Tally {

        std::uint32_t stage_refusals = 0;

        std::uint32_t save_mount_collisions = 0;

        std::uint32_t channel_release_refusals = 0;

        std::uint32_t prefetch_park_timeouts = 0;

        std::uint32_t prefetch_park_deferrals = 0;
    };

    struct Wiring {
        hal::ISpiTransport& link;
        LinkBinder& binder;
        const os::IClock& clock;
        BoardOps ops;
        ICheatApply& cheats;
        const SessionBindings& bindings;
        LinkRxChannel<SeatTag::Main>& link_rx;
        SessionMachine& machine;
        infra::ErrorSite advise_site;
        SessionTally& session_tally;
        CoreOptionCells& options;
        xthread::LogLane& log_lane;

        infra::OptRef<const svc::Vfs> vfs{};

        infra::OptRef<svc::IStorageLifecycle> storage_life{};

        infra::OptRef<svc::ChdPrefetch> prefetch{};

        infra::OptRef<proto::IStorageChannel> storage_channel{};
    };

    explicit SlotBindings(const Wiring& g) noexcept;

    SlotBindings(const SlotBindings&) = delete;
    SlotBindings& operator=(const SlotBindings&) = delete;

    [[nodiscard]] proto::BlockSlots& slots() noexcept { return slots_; }

    [[nodiscard]] const proto::BlockSlots& slots() const noexcept { return slots_; }

    [[nodiscard]] const SaveImageSource& save_source() const noexcept { return *save_src_; }

    [[nodiscard]] const MountStatusCell& mount_status_cell() const noexcept {
        return mount_status_cell_;
    }

    [[nodiscard]] const SaveExtentCell& save_extent_cell() const noexcept {
        return save_extent_cell_;
    }

    [[nodiscard]] proto::BlockSlotMask channel_borrows() const noexcept { return channel_borrows_; }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

    using Result = ILinkEncoder::Outcome;
    Result on(const proto::LinkOp::BindSlot& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::BindMount& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::StageMount& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::AnnounceMount& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::StageCheats& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::AttachSave& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::DetachSave& a, const LinkOpCtx& ctx) noexcept;
    Result misrouted(const proto::LinkOp& op, const LinkOpCtx& ctx) noexcept;

    void mount_save_channel(
        cores::ISaveChannel& channel,
        proto::BlockSlotMask poisoned,
        bool remounts_saves
    );

    void settle(infra::OptRef<cores::Core> core) noexcept;

    void settle_slot_mount(infra::OptRef<cores::Core> core) noexcept;

    void drop_bindings() noexcept;

    void note_drop_park() noexcept;

    void forget_channel_borrows() noexcept;

    void bind_core_roles(infra::OptRef<cores::Core> core) noexcept;

    [[nodiscard]] Ex<void> apply_bind_slot(
        proto::SlotIndex slot,
        proto::LinkOp::SlotBind kind,
        proto::FileSize size,
        proto::PathId path_id,
        proto::IBlockRoles& roles
    );

private:
    static constexpr std::chrono::milliseconds kQuiesceBudget{250};

    static constexpr unsigned kOffSlot = SaveImageSource::kSlots;

    enum class SaveBind : std::uint8_t { Ready, Waiting, Declined };

    [[nodiscard]] const os::IClock& clock() const noexcept { return clock_; }

    void unmount_save_channel();

    void release_channel_slots_(proto::BlockSlotMask mask, proto::BlockSlotMask kept) noexcept;

    void begin_detach_(proto::SlotIndex slot) noexcept;
    void release_save_slots_() noexcept;
    [[nodiscard]] Ex<SaveBind> bind_save_(
        proto::SlotIndex slot,
        std::string_view path,
        bool manual,
        infra::OptRef<cores::Core> core
    );
    [[nodiscard]] proto::FileSize save_extent_of_(proto::SlotIndex slot) const noexcept;

    [[nodiscard]] Ex<void> apply_bind_slot_(
        proto::SlotIndex slot,
        proto::LinkOp::SlotBind kind,
        proto::FileSize size,
        proto::PathId path_id,
        proto::IBlockRoles& roles
    );

    [[nodiscard]] Ex<void> announce_disc_(
        proto::SlotIndex slot,
        proto::FileSize size,
        proto::IBlockRoles& roles
    );

    [[nodiscard]] cores::MountStatus mount_body_(infra::OptRef<cores::Core> core) noexcept;
    [[nodiscard]] static cores::SaveExtent save_body_(
        proto::FileSize size_bytes,
        bool exists = false
    ) noexcept;

    [[nodiscard]] ILinkEncoder::Outcome perform_bound_mount_(
        const proto::LinkOp::BindMount& bm,
        const std::optional<SessionBindings::Mount>& image,
        infra::OptRef<cores::Core> core
    );

    [[nodiscard]] ILinkEncoder::Outcome owed_save_bind_(
        OwedSaveBind& owed,
        bool attach,
        proto::SlotIndex slot,
        proto::ActKey act,
        proto::FileId path,
        bool manual,
        const ILinkTxReader& inbox,
        infra::OptRef<cores::Core> core
    ) noexcept;

    void settle_save_binds_() noexcept;
    void settle_stage_mount_(infra::OptRef<cores::Core> core) noexcept;
    [[nodiscard]] cores::MountState present_mount_(
        infra::OptRef<cores::Core> core,
        proto::SlotIndex slot,
        std::string_view path
    ) noexcept;

    void after_mount_answer_(infra::OptRef<cores::Core> core) noexcept;

    hal::ISpiTransport& link_;
    LinkBinder& binder_;
    const os::IClock& clock_;
    const BoardOps ops_;

    ICheatApply& cheats_;

    const SessionBindings& bindings_;
    SessionMachine& machine_;
    const infra::ErrorSite advise_site_;
    SessionTally& session_tally_;
    CoreOptionCells& options_;
    xthread::LogLane::Producer log_lane_;
    infra::OptRef<const svc::Vfs> vfs_;
    infra::OptRef<svc::IStorageLifecycle> storage_life_;
    infra::OptRef<svc::ChdPrefetch> prefetch_;
    MountStatusCell mount_status_cell_{};
    SaveExtentCell save_extent_cell_{};

    OwedMount owed_mount_;
    std::array<OwedSaveBind, SaveImageSource::kSlots + 1> owed_save_;

    std::unique_ptr<SaveImageSource> save_src_;

    proto::BlockSlots slots_;
    [[no_unique_address]] proto::DiscAnnounce announce_{};
    static_assert(
        sizeof(proto::BlockSlots) < 4096,
        "slots_ is an inline member of a "
        "T-RT type; it must stay small"
    );

    proto::BlockSlotMask channel_slots_{};

    proto::BlockSlotMask channel_borrows_{};
    Tally tally_{};
};

static_assert(LinkOpMountSink<SlotBindings>);

}
