// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "app/board_ops.h"
#include "app/cheat_apply.h"
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
#include "proto/block_slots.h"
#include "proto/disc_announce.h"
#include "proto/download_session.h"
#include "proto/image_bracket.h"
#include "proto/link_op.h"
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
        LinkRxChannel<SeatTag::Main>& link_rx;
        SessionMachine& machine;
        EmitSite advise_site;
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

    [[nodiscard]] std::uint16_t channel_borrows() const noexcept { return channel_borrows_; }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

    using Result = ILinkEncoder::Outcome;
    Result on(const proto::LinkOp::BindSlot& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::BindMount& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::StageMount& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::AnnounceMount& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::StageCheats& a, const LinkOpCtx& ctx) noexcept;
    Result misrouted(const proto::LinkOp& op, const LinkOpCtx& ctx) noexcept;

    void mount_save_channel(cores::ISaveChannel& channel, std::uint16_t poisoned);

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
        proto::PathId path_id
    );

private:
    static constexpr std::int64_t kQuiesceBudgetNs = 250'000'000;

    static constexpr std::size_t kMaxChannelSlots = proto::kMaxAnnounceableSlot + 1;

    static constexpr unsigned kOffSlot = SaveImageSource::kSlots;

    enum class SaveBind : std::uint8_t { Ready, Waiting, Declined };

    [[nodiscard]] const os::IClock& clock() const noexcept { return clock_; }

    void unmount_save_channel();

    void release_channel_slots_(std::uint16_t mask, std::size_t keep) noexcept;

    void begin_detach_(proto::SlotIndex slot) noexcept;
    void release_save_slots_() noexcept;
    [[nodiscard]] Ex<SaveBind> bind_save_(
        proto::SlotIndex slot,
        std::string_view path,
        bool manual,
        infra::OptRef<cores::Core> core
    );
    [[nodiscard]] std::uint64_t save_extent_of_(proto::SlotIndex slot) const noexcept;

    [[nodiscard]] Ex<void> apply_bind_slot_(
        proto::SlotIndex slot,
        proto::LinkOp::SlotBind kind,
        proto::FileSize size,
        proto::PathId path_id
    );

    [[nodiscard]] Ex<void> announce_disc_(proto::SlotIndex slot, proto::FileSize size);

    void apply_bind_slot_roles_(proto::SlotIndex slot, const proto::SlotRoles& roles) noexcept;

    [[nodiscard]] proto::SlotRoles current_slot_roles_(infra::OptRef<cores::Core> core) noexcept;

    [[nodiscard]] cores::MountStatus mount_body_(infra::OptRef<cores::Core> core) noexcept;
    [[nodiscard]] static cores::SaveExtent save_body_(std::uint64_t size_bytes) noexcept;

    [[nodiscard]] ILinkEncoder::Outcome perform_bound_mount_(
        const proto::LinkOp::BindMount& bm,
        const std::optional<SessionBindings::Mount>& image,
        infra::OptRef<cores::Core> core
    );
    [[nodiscard]] ILinkEncoder::Outcome apply_save_bind_(
        const proto::LinkOp::BindSlot& bind,
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
    SessionMachine& machine_;
    const EmitSite advise_site_;
    SessionTally& session_tally_;
    CoreOptionCells& options_;
    xthread::LogLane& log_lane_;
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

    std::uint16_t channel_slots_ = 0;

    std::uint16_t channel_borrows_ = 0;
    Tally tally_{};
};

inline SlotBindings::SlotBindings(const Wiring& g) noexcept
    : link_(g.link),
      binder_(g.binder),
      clock_(g.clock),
      ops_(g.ops),
      cheats_(g.cheats),
      machine_(g.machine),
      advise_site_(g.advise_site),
      session_tally_(g.session_tally),
      options_(g.options),
      log_lane_(g.log_lane),
      vfs_(g.vfs),
      storage_life_(g.storage_life),
      prefetch_(g.prefetch),
      owed_mount_(mount_status_cell_, g.link_rx),
      owed_save_(make_owed_saves<SaveImageSource::kSlots + 1>(save_extent_cell_, g.link_rx)),
      save_src_(vfs_ ? std::make_unique<SaveImageSource>(*vfs_) : nullptr),
      slots_(
          proto::BlockSlots::Wiring{
              .source = save_src_ ? infra::OptRef<proto::IImageSource>{*save_src_}
                                  : infra::OptRef<proto::IImageSource>{},
              .clock = infra::OptRef<const os::IClock>{clock_},
              .channel = g.storage_channel
          }
      ) {
    slots_.set_storage_live(g.storage_life.has_value());
}

inline ILinkEncoder::Outcome SlotBindings::on(
    const proto::LinkOp::AnnounceMount& a,
    const LinkOpCtx&
) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    const proto::SlotIndex si = a.slot;
    if (si.v >= proto::kBlockSlots) {
        return ILinkEncoder::Outcome::Dropped;
    }

    if (auto r = announce_.flag_form(link_, a.loaded); !r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

inline ILinkEncoder::Outcome SlotBindings::on(
    const proto::LinkOp::BindSlot& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    if (a.bind == proto::LinkOp::SlotBind::Attach || a.bind == proto::LinkOp::SlotBind::Detach) {
        return apply_save_bind_(a, ctx.inbox, ctx.core);
    }
    const proto::SlotIndex si = a.slot;
    if (si.v >= proto::kBlockSlots) {
        return ILinkEncoder::Outcome::Dropped;
    }

    if (ctx.core.has_value()) {
        apply_bind_slot_roles_(
            proto::SlotIndex{ctx.core->profile().staging.disc_slot.v},
            current_slot_roles_(ctx.core)
        );
    }

    const proto::LinkOp::SlotBind kind = a.bind;
    if (kind == proto::LinkOp::SlotBind::Mount && a.path == proto::FileId{}) {
        return ILinkEncoder::Outcome::Encoded;
    }
    proto::FileSize size{};
    if (a.path != proto::FileId{}) {
        const InternedFile* const file = ctx.inbox.file(a.path);
        if (file == nullptr)
            return ILinkEncoder::Outcome::Dropped;
        size = proto::FileSize{file->size_bytes};
    }
    const proto::PathId path_id{a.path.v};
    if (!a.bracketed) {
        if (a.sends_index && kind != proto::LinkOp::SlotBind::MountCd) {
            (void)binder_.fio_queue().flush();
            if (auto r = proto::DownloadSession::set_index(link_, proto::IoIndex{a.index_word}); !r)
                return ILinkEncoder::Outcome::Dropped;
        }
        if (auto r = apply_bind_slot_(si, kind, size, path_id); !r) {
            return ILinkEncoder::Outcome::Dropped;
        }
        return ILinkEncoder::Outcome::Encoded;
    }

    cores::IStagingCore* const sd = ctx.core.has_value() ? ctx.core->staging_core() : nullptr;
    if (sd == nullptr)
        return ILinkEncoder::Outcome::Dropped;
    auto frame = proto::ImageBracket::open(sd->image_sink(), ctx.core->profile().staging.save_dest);
    if (!frame)
        return ILinkEncoder::Outcome::Dropped;
    const auto bound = apply_bind_slot_(si, kind, size, path_id);
    const auto closed = frame->end();
    if (!bound || !closed)
        return ILinkEncoder::Outcome::Dropped;
    return ILinkEncoder::Outcome::Encoded;
}

inline ILinkEncoder::Outcome SlotBindings::on(
    const proto::LinkOp::BindMount& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    const auto image = SessionBindings::decode_mount(a, ctx.inbox);
    if (!image) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return perform_bound_mount_(a, *image, ctx.core);
}

inline ILinkEncoder::Outcome SlotBindings::on(
    const proto::LinkOp::StageMount& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    const bool slot_mount = ctx.core.has_value() && ctx.core->slot_mount() != nullptr;
    const bool staged = ctx.core.has_value() && ctx.core->staging_core() != nullptr;
    const InternedFile* const file = a.path == proto::FileId{} ? nullptr : ctx.inbox.file(a.path);
    const std::string_view path = file == nullptr ? std::string_view{} : file->path.view();

    const bool parked = !prefetch_ || slot_mount;
    const std::int64_t quiesce_ns = parked ? 0 : clock().now().count() + kQuiesceBudgetNs;

    owed_mount_.arm(a.act_gen, mount_body_(ctx.core), a.slot, path, parked, quiesce_ns);
    if ((!slot_mount && !staged) || (a.path != proto::FileId{} && file == nullptr)) {
        owed_mount_.answer(cores::MountVerdict::Failed, mount_body_(ctx.core));
        return ILinkEncoder::Outcome::Dropped;
    }
    if (!parked) {
        prefetch_->request_park();
        return ILinkEncoder::Outcome::Encoded;
    }
    const cores::MountState ms = present_mount_(ctx.core, a.slot, path);
    if (ms == cores::MountState::Pending) {

        return ILinkEncoder::Outcome::Encoded;
    }
    after_mount_answer_(ctx.core);
    owed_mount_.answer(
        ms == cores::MountState::Done ? cores::MountVerdict::Done : cores::MountVerdict::Failed,
        mount_body_(ctx.core)
    );
    return ILinkEncoder::Outcome::Encoded;
}

inline ILinkEncoder::Outcome SlotBindings::on(
    const proto::LinkOp::StageCheats& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    if (!ctx.core.has_value())
        return ILinkEncoder::Outcome::Dropped;
    const InternedFile* const file = ctx.inbox.file(a.path);
    if (file == nullptr)
        return ILinkEncoder::Outcome::Dropped;
    cheats_.content_loaded(&*ctx.core, TxDigest::Kind::Mount, file->path.view(), 0, a.same_game);
    return ILinkEncoder::Outcome::Encoded;
}

inline ILinkEncoder::Outcome SlotBindings::misrouted(
    const proto::LinkOp&,
    const LinkOpCtx&
) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    ++session_tally_.link_op_misrouted;
    return ILinkEncoder::Outcome::Dropped;
}

inline void SlotBindings::settle(infra::OptRef<cores::Core> core) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    settle_save_binds_();
    settle_stage_mount_(core);
}

inline void SlotBindings::settle_slot_mount(infra::OptRef<cores::Core> core) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    if (core.has_value() && core->slot_mount() != nullptr)
        settle_stage_mount_(core);
}

inline void SlotBindings::drop_bindings() noexcept {
    TASTY_SEAT_BODY(SlotBindings);

    owed_mount_.strand();
    for (OwedSaveBind& owed : owed_save_)
        owed.strand();

    channel_slots_ = 0;
    slots_.reset_all();

    release_save_slots_();
    if (save_src_ != nullptr)
        save_src_->unbind_all();
}

inline void SlotBindings::note_drop_park() noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    if (prefetch_) {
        if (!prefetch_->park_acked()) {
            session_tally_.last_error = Error{Errc::would_block, ERR_SITE(), 0};
            ++tally_.prefetch_park_deferrals;
        }
    }
}

inline void SlotBindings::forget_channel_borrows() noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    channel_borrows_ = 0;
}

inline void SlotBindings::bind_core_roles(infra::OptRef<cores::Core> core) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    if (core.has_value()) {
        apply_bind_slot_roles_(
            proto::SlotIndex{core->profile().staging.disc_slot.v},
            current_slot_roles_(core)
        );
    }
}

[[nodiscard]] inline Ex<void> SlotBindings::apply_bind_slot(
    proto::SlotIndex slot,
    proto::LinkOp::SlotBind kind,
    proto::FileSize size,
    proto::PathId path_id
) {
    TASTY_SEAT_BODY(SlotBindings);
    return apply_bind_slot_(slot, kind, size, path_id);
}

[[nodiscard]] inline Ex<SlotBindings::SaveBind> SlotBindings::bind_save_(
    proto::SlotIndex si,
    std::string_view path,
    bool manual,
    infra::OptRef<cores::Core> core
) {
    TASTY_SEAT_BODY(SlotBindings);
    if (save_src_ == nullptr || path.empty())
        return SaveBind::Ready;
    using Attach = SaveImageSource::Attach;
    switch (save_src_->attach_state(si)) {
        case Attach::Attached:
            if (save_src_->path_of(si) == path
                && save_src_->manual(si) == manual
                && !save_src_->close_owed(si))
                return SaveBind::Ready;
            begin_detach_(si);
            return SaveBind::Declined;
        case Attach::Attaching:
        case Attach::Detaching:

            return SaveBind::Declined;
        case Attach::Detached:
            break;
    }

    const cores::BlankSaveSpec blank = core.has_value() ? core->profile().blank_save
                                                        : cores::BlankSaveSpec{};
    if (!save_src_->bind(si, path, manual, blank)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), si.v});
    }

    if (!storage_life_) {
        save_src_->note_attached(si, false, proto::FileSize{});
        return SaveBind::Ready;
    }
    svc::IStorageBackend* const backend = save_src_->backend(si);
    if (backend == nullptr)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), si.v});

    if (!storage_life_->stage_backend(si, *backend, save_src_->order(si)))
        return SaveBind::Declined;
    if (!slots_.submit_attach(si)) {
        ++tally_.stage_refusals;
        save_src_->note_attached(si, false, proto::FileSize{});
        return SaveBind::Ready;
    }
    save_src_->mark(si, Attach::Attaching);
    return SaveBind::Waiting;
}

inline std::uint64_t SlotBindings::save_extent_of_(proto::SlotIndex slot) const noexcept {
    return save_src_ == nullptr ? 0 : save_src_->size_of(slot).v;
}

inline void SlotBindings::begin_detach_(proto::SlotIndex si) noexcept {
    using Attach = SaveImageSource::Attach;
    if (save_src_ == nullptr)
        return;
    const Attach state = save_src_->attach_state(si);
    if (state != Attach::Attached && state != Attach::Attaching)
        return;

    if (!storage_life_) {
        save_src_->note_detached(si);
        return;
    }

    if (state == Attach::Attaching) {
        save_src_->owe_close(si);
        return;
    }

    if (!slots_.submit_detach(si)) {
        ++tally_.stage_refusals;
        return;
    }
    save_src_->mark(si, Attach::Detaching);
}

inline void SlotBindings::release_save_slots_() noexcept {
    for (unsigned i = 0; i < SaveImageSource::kSlots; ++i) {
        begin_detach_(proto::SlotIndex{static_cast<std::uint8_t>(i)});
    }
}

inline ILinkEncoder::Outcome SlotBindings::perform_bound_mount_(
    const proto::LinkOp::BindMount& bm,
    const std::optional<SessionBindings::Mount>& image,
    infra::OptRef<cores::Core> core
) {
    if (!bm.perform)
        return ILinkEncoder::Outcome::Encoded;
    if (!core.has_value()) {
        machine_.advise(Event::InfoRequest{.id = InfoId::ImageMountFailed}, advise_site_);
        return ILinkEncoder::Outcome::Dropped;
    }
    RtRelaxScope scope(ops_);
    Ex<void> r{};
    if (bm.image == proto::FileId{}) {
        r = core->mount(bm.index, cores::MountedPath{});
    } else if (image.has_value()) {
        r = core->mount(
            bm.index,
            cores::MountedPath{
                proto::PathId{0},
                image->path.view(),
                proto::FileSize{image->size_bytes}
            }
        );
    } else {
        r = std::unexpected(Error{Errc::not_found, ERR_SITE(), bm.index.v});
    }
    if (!r) {

        session_tally_.last_error = r.error();
        machine_.advise(Event::InfoRequest{.id = InfoId::ImageMountFailed}, advise_site_);
    }
    options_.publish_option_table(core);
    return ILinkEncoder::Outcome::Encoded;
}

inline ILinkEncoder::Outcome SlotBindings::apply_save_bind_(
    const proto::LinkOp::BindSlot& bind,
    const ILinkTxReader& inbox,
    infra::OptRef<cores::Core> core
) noexcept {
    const proto::SlotIndex si = bind.slot;
    const bool pendable = si.v < SaveImageSource::kSlots;

    OwedSaveBind& owed = owed_save_[pendable ? si.v : kOffSlot];
    owed.arm(bind.act_gen, save_body_(0));
    if (bind.bind != proto::LinkOp::SlotBind::Attach) {
        begin_detach_(si);

        const bool refused = save_src_ != nullptr
            && save_src_->attach_state(si) == SaveImageSource::Attach::Attached;
        owed.answer(
            refused ? cores::SaveVerdict::Declined : cores::SaveVerdict::Known,
            save_body_(0)
        );
        return ILinkEncoder::Outcome::Encoded;
    }
    const InternedFile* const file = inbox.file(bind.path);
    if (file == nullptr) {
        owed.answer(cores::SaveVerdict::Declined, save_body_(0));
        return ILinkEncoder::Outcome::Dropped;
    }
    auto bound = bind_save_(si, file->path.view(), bind.manual, core);
    if (!bound) {
        owed.answer(cores::SaveVerdict::Declined, save_body_(0));
        return ILinkEncoder::Outcome::Dropped;
    }
    switch (*bound) {
        case SaveBind::Ready:
            owed.answer(cores::SaveVerdict::Known, save_body_(save_extent_of_(si)));
            break;
        case SaveBind::Waiting:
            break;
        case SaveBind::Declined:
            owed.answer(cores::SaveVerdict::Declined, save_body_(0));
            break;
    }
    return ILinkEncoder::Outcome::Encoded;
}

inline void SlotBindings::settle_save_binds_() noexcept {
    if (save_src_ == nullptr)
        return;
    using Attach = SaveImageSource::Attach;
    for (unsigned i = 0; i < SaveImageSource::kSlots; ++i) {
        const proto::SlotIndex si{static_cast<std::uint8_t>(i)};

        if (save_src_->close_owed(si) && save_src_->attach_state(si) == Attach::Attached)
            begin_detach_(si);
        if (!owed_save_[i].owed())
            continue;
        const Attach state = save_src_->attach_state(si);
        if (state == Attach::Attaching)
            continue;

        const bool known = state == Attach::Attached;
        owed_save_[i].answer(
            known ? cores::SaveVerdict::Known : cores::SaveVerdict::Declined,
            save_body_(known ? save_extent_of_(si) : 0)
        );
    }
}

inline void SlotBindings::settle_stage_mount_(infra::OptRef<cores::Core> core) noexcept {
    if (!owed_mount_.owed())
        return;

    if (!owed_mount_.parked()) {
        if (prefetch_ && !prefetch_->park_acked()) {
            if (clock().now().count() < owed_mount_.quiesce_deadline_ns()) {
                ++tally_.prefetch_park_deferrals;
                return;
            }

            session_tally_.last_error = Error{Errc::timeout, ERR_SITE(), 0};
            ++tally_.prefetch_park_timeouts;
        }
        owed_mount_.mark_parked();
    }
    const cores::MountState ms = present_mount_(core, owed_mount_.slot(), owed_mount_.path());
    if (ms == cores::MountState::Pending)
        return;
    after_mount_answer_(core);
    owed_mount_.answer(
        ms == cores::MountState::Done ? cores::MountVerdict::Done : cores::MountVerdict::Failed,
        mount_body_(core)
    );
}

inline cores::MountState SlotBindings::present_mount_(
    infra::OptRef<cores::Core> core,
    proto::SlotIndex slot,
    std::string_view path
) noexcept {
    if (!core.has_value())
        return cores::MountState::Failed;
    if (cores::ISlotMount* sm = core->slot_mount(); sm != nullptr)
        return sm->mount_slot(slot, path);
    cores::IStagingCore* cd = core->staging_core();
    return cd == nullptr ? cores::MountState::Failed : cd->mount_disc(path);
}

inline void SlotBindings::after_mount_answer_(infra::OptRef<cores::Core> core) noexcept {
    options_.publish_option_table(core);
}

inline cores::MountStatus SlotBindings::mount_body_(infra::OptRef<cores::Core> core) noexcept {
    cores::MountStatus body{};
    if (cores::IStagingCore* cd = core.has_value() ? core->staging_core() : nullptr;
        cd != nullptr) {
        const cores::StagePlan plan = cd->stage_plan();
        body.disc_size_bytes = plan.disc_size_bytes;
        body.data_first_track = plan.data_first_track;
        body.game_id = plan.game_id;
    }
    return body;
}

inline cores::SaveExtent SlotBindings::save_body_(std::uint64_t size_bytes) noexcept {
    cores::SaveExtent body{};
    body.size_bytes = size_bytes;
    return body;
}

[[nodiscard]] inline Ex<void> SlotBindings::apply_bind_slot_(
    proto::SlotIndex slot,
    proto::LinkOp::SlotBind kind,
    proto::FileSize size,
    proto::PathId path_id
) {
    switch (kind) {
        case proto::LinkOp::SlotBind::Mount:
            return slots_.mount(link_, slot, size, path_id);
        case proto::LinkOp::SlotBind::Unmount:
            return slots_.unmount(link_, slot);
        case proto::LinkOp::SlotBind::MountCd:
            return announce_disc_(slot, size);
        case proto::LinkOp::SlotBind::Attach:
        case proto::LinkOp::SlotBind::Detach:
            break;
    }
    return std::unexpected(
        Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(infra::ordinal(kind))}
    );
}

[[nodiscard]] inline Ex<void> SlotBindings::announce_disc_(
    proto::SlotIndex slot,
    proto::FileSize size
) {
    return announce_.mount_disc(slots_, link_, slot, size);
}

inline proto::SlotRoles SlotBindings::current_slot_roles_(
    infra::OptRef<cores::Core> core
) noexcept {
    proto::IResidentImageSource* const resident = core->block_source();
    proto::IBlockGeometry* const geometry = core->block_geometry();
    return proto::SlotRoles{
        .resident = resident != nullptr ? infra::OptRef<proto::IResidentImageSource>{*resident}
                                        : infra::OptRef<proto::IResidentImageSource>{},
        .geometry = geometry != nullptr ? infra::OptRef<proto::IBlockGeometry>{*geometry}
                                        : infra::OptRef<proto::IBlockGeometry>{}
    };
}

inline void SlotBindings::apply_bind_slot_roles_(
    proto::SlotIndex slot,
    const proto::SlotRoles& roles
) noexcept {
    slots_.bind_roles(slot, roles);
}

inline void SlotBindings::mount_save_channel(cores::ISaveChannel& channel, std::uint16_t poisoned) {
    TASTY_SEAT_BODY(SlotBindings);
    if (!vfs_)
        return;
    const auto count = channel.claim_mount_plan();
    if (!count)
        return;
    const std::uint16_t previous = channel_slots_;
    unmount_save_channel();

    release_channel_slots_(previous, *count);

    cores::ISaveChannel::Mount mounts[kMaxChannelSlots]{};
    bool planned[kMaxChannelSlots]{};
    const std::size_t n = *count < kMaxChannelSlots ? *count : kMaxChannelSlots;
    for (std::size_t i = 0; i < n; ++i) {
        auto m = channel.mount_at(i);
        if (!m) {
            session_tally_.last_error = m.error();
            continue;
        }
        mounts[i] = *m;
        planned[i] = true;
    }

    for (std::size_t i = 0; i < n; ++i) {
        if (!planned[i])
            continue;
        const proto::SlotIndex si{mounts[i].slot.v};

        if ((poisoned & (1u << si.v)) != 0u) {
            ++tally_.stage_refusals;
            session_tally_.last_error = Error{Errc::would_block, ERR_SITE(), si.v};
            continue;
        }
        if (!storage_life_) {
            channel.descriptor().note_attached(si, false, proto::FileSize{});
        } else {
            svc::IStorageBackend* const b = channel.backend(i);
            const bool staged = b != nullptr
                && storage_life_->stage_backend(si, *b, channel.order(i));
            if (staged)
                channel_borrows_ = static_cast<std::uint16_t>(channel_borrows_ | (1u << si.v));
            if (!staged || !slots_.submit_attach(si)) {
                ++tally_.stage_refusals;
                channel.descriptor().note_attached(si, false, proto::FileSize{});
            }
        }
        apply_bind_slot_roles_(
            si,
            proto::SlotRoles{.descriptor = infra::OptRef<proto::IImageSource>{channel.descriptor()}}
        );
        if (auto r = apply_bind_slot_(
                si,
                proto::LinkOp::SlotBind::Mount,
                mounts[i].size,
                proto::PathId{0}
            );
            !r) {
            apply_bind_slot_roles_(si, proto::SlotRoles{});
            session_tally_.last_error = r.error();
            continue;
        }
        channel_slots_ = static_cast<std::uint16_t>(channel_slots_ | (1u << si.v));
        (void)log_lane_.push(LogRec::Mount{si.v}, log_now_ns());
    }
}

inline void SlotBindings::unmount_save_channel() {
    for (std::uint8_t i = 0; i < proto::kBlockSlots; ++i) {
        if ((channel_slots_ & (1u << i)) == 0u)
            continue;
        const proto::SlotIndex si{i};
        apply_bind_slot_roles_(si, proto::SlotRoles{});
        if (auto r = apply_bind_slot_(
                si,
                proto::LinkOp::SlotBind::Unmount,
                proto::FileSize{},
                proto::PathId{}
            );
            r)
            (void)log_lane_.push(LogRec::Unmount{si.v}, log_now_ns());
    }
    channel_slots_ = 0;
}

inline void SlotBindings::release_channel_slots_(std::uint16_t mask, std::size_t keep) noexcept {
    (void)slots_.install_completions();
    for (std::uint8_t i = 0; i < proto::kBlockSlots; ++i) {
        if ((mask & (1u << i)) == 0u)
            continue;
        const proto::SlotIndex si{i};
        if (i >= keep && storage_life_) {
            if (!slots_.submit_detach(si))
                ++tally_.stage_refusals;
            continue;
        }
        if (!storage_life_)
            continue;
        if (storage_life_->release_slot(si)) {
            channel_borrows_ = static_cast<std::uint16_t>(channel_borrows_ & ~(1u << i));
        } else {
            ++tally_.channel_release_refusals;
        }
    }
}

static_assert(LinkOpMountSink<SlotBindings>);

}
