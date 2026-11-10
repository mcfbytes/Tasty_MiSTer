// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/slot_bindings.h"

namespace mister::app {

SlotBindings::SlotBindings(const Wiring& g) noexcept
    : link_(g.link),
      binder_(g.binder),
      clock_(g.clock),
      ops_(g.ops),
      cheats_(g.cheats),
      bindings_(g.bindings),
      machine_(g.machine),
      advise_site_(g.advise_site),
      session_tally_(g.session_tally),
      options_(g.options),
      log_lane_(g.log_lane.producer()),
      vfs_(g.vfs),
      storage_life_(g.storage_life),
      prefetch_(g.prefetch),
      owed_mount_(mount_status_cell_, g.link_rx),
      owed_save_(make_owed_saves<SaveImageSource::kSlots + 1>(save_extent_cell_, g.link_rx)),
      save_src_(vfs_ ? std::make_unique<SaveImageSource>(*vfs_) : nullptr),
      slots_(
          proto::BlockSlots::Wiring{
              .link = link_,
              .source = save_src_ ? infra::OptRef<proto::IImageSource>{*save_src_}
                                  : infra::OptRef<proto::IImageSource>{},
              .clock = infra::OptRef<const os::IClock>{clock_},
              .channel = g.storage_channel
          }
      ) {
    slots_.set_storage_live(g.storage_life.has_value());
}

ILinkEncoder::Outcome SlotBindings::on(
    const proto::LinkOp::AnnounceMount& a,
    const LinkOpCtx&
) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    const proto::SlotIndex si = a.slot;
    if (!si.in_block_slots()) {
        return ILinkEncoder::Outcome::Dropped;
    }

    proto::SpiFrame frame{link_};
    if (auto r = announce_.flag_form(frame, a.loaded); !r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SlotBindings::on(
    const proto::LinkOp::BindSlot& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    const proto::SlotIndex si = a.slot;
    if (!si.in_block_slots()) {
        return ILinkEncoder::Outcome::Dropped;
    }

    bind_core_roles(ctx.core);

    proto::NoBlockRoles none;
    std::optional<CoreBlockRoles> core_roles;
    if (ctx.core.has_value())
        core_roles.emplace(*ctx.core);
    proto::IBlockRoles& roles = core_roles.has_value()
        ? static_cast<proto::IBlockRoles&>(*core_roles)
        : none;

    const proto::LinkOp::SlotBind kind = a.bind;
    if (kind == proto::LinkOp::SlotBind::Mount && a.path == proto::FileId{}) {
        return ILinkEncoder::Outcome::Encoded;
    }
    proto::FileSize size{};
    if (a.path != proto::FileId{}) {
        const infra::OptRef<const InternedFile> file = ctx.tx.file(a.path);
        if (!file)
            return ILinkEncoder::Outcome::Dropped;
        size = file->size_bytes;
    }
    const proto::PathId path_id = a.path.path();
    if (!a.bracketed) {
        if (a.sends_index && kind != proto::LinkOp::SlotBind::MountCd) {
            (void)binder_.fio_queue().flush();
            proto::SpiFrame frame{link_};
            if (auto r = proto::DownloadSession::set_index(frame, proto::IoIndex{a.index_word}); !r)
                return ILinkEncoder::Outcome::Dropped;
        }
        if (auto r = apply_bind_slot_(si, kind, size, path_id, roles); !r) {
            return ILinkEncoder::Outcome::Dropped;
        }
        return ILinkEncoder::Outcome::Encoded;
    }

    if (!ctx.core.has_value())
        return ILinkEncoder::Outcome::Dropped;
    const cores::StagingPolicy& staging = ctx.core->profile().staging;
    if (!staging.save || !staging.save->bracket || !ctx.core->staging_core())
        return ILinkEncoder::Outcome::Dropped;

    proto::FixedBlockRoles bracketed_roles{roles.resolve(slots_.server(si))};
    auto frame = proto::ImageBracket::open(ctx.core->image_sink(), *staging.save->bracket);
    if (!frame)
        return ILinkEncoder::Outcome::Dropped;
    const auto bound = apply_bind_slot_(si, kind, size, path_id, bracketed_roles);
    const auto closed = frame->end();
    if (!bound || !closed)
        return ILinkEncoder::Outcome::Dropped;
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SlotBindings::on(
    const proto::LinkOp::BindMount& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    const auto image = SessionBindings::decode_mount(a, ctx.tx);
    if (!image) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return perform_bound_mount_(a, *image, ctx.core);
}

ILinkEncoder::Outcome SlotBindings::on(
    const proto::LinkOp::StageMount& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    const bool slot_mount = ctx.core.has_value() && ctx.core->slot_mount();
    const bool staged = ctx.core.has_value() && ctx.core->staging_core();
    infra::OptRef<const InternedFile> file{};
    if (a.path != proto::FileId{})
        file = ctx.tx.file(a.path);
    const std::string_view path = file ? file->path.view() : std::string_view{};

    const bool parked = !prefetch_ || slot_mount;
    const std::optional<os::Deadline> quiesce = parked
        ? std::nullopt
        : std::optional<os::Deadline>{os::Deadline::in(clock(), kQuiesceBudget)};

    owed_mount_.arm(a.act_gen, mount_body_(ctx.core), a.slot, path, quiesce);
    if ((!slot_mount && !staged) || (a.path != proto::FileId{} && !file)) {
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

ILinkEncoder::Outcome SlotBindings::on(
    const proto::LinkOp::StageCheats& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    if (!ctx.core.has_value())
        return ILinkEncoder::Outcome::Dropped;
    const infra::OptRef<const InternedFile> file = ctx.tx.file(a.path);
    if (!file)
        return ILinkEncoder::Outcome::Dropped;
    TxDigest loaded{};
    loaded.gen = bindings_.generation();
    loaded.kind = TxDigest::Kind::Mount;
    loaded.same_game = a.same_game;
    (void)loaded.path.assign(file->path.view());
    cheats_.content_loaded(*ctx.core, loaded);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SlotBindings::misrouted(const proto::LinkOp&, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    ++session_tally_.link_op_misrouted;
    return ILinkEncoder::Outcome::Dropped;
}

void SlotBindings::settle(infra::OptRef<cores::Core> core) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    settle_save_binds_();
    settle_stage_mount_(core);
}

void SlotBindings::settle_slot_mount(infra::OptRef<cores::Core> core) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    if (core.has_value() && core->slot_mount())
        settle_stage_mount_(core);
}

void SlotBindings::drop_bindings() noexcept {
    TASTY_SEAT_BODY(SlotBindings);

    owed_mount_.strand();
    for (OwedSaveBind& owed : owed_save_)
        owed.strand();

    channel_slots_ = {};
    slots_.reset_all();

    release_save_slots_();
    if (save_src_ != nullptr)
        save_src_->unbind_all();
}

void SlotBindings::note_drop_park() noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    if (prefetch_) {
        if (!prefetch_->park_acked()) {
            session_tally_.last_error = Error{Errc::would_block, ERR_SITE(), 0};
            ++tally_.prefetch_park_deferrals;
        }
    }
}

void SlotBindings::forget_channel_borrows() noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    channel_borrows_ = {};
}

void SlotBindings::bind_core_roles(infra::OptRef<cores::Core> core) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    if (!core.has_value())
        return;
    const proto::SlotIndex slot = core->profile().staging.disc_slot.block_slot();

    if (slots_.server(slot) == proto::SlotServer::CoreDescriptor)
        return;
    if (core->block_server()) {

        slots_.bind_server(slot, proto::SlotServer::CoreBlock);
    } else if (slots_.server(slot) == proto::SlotServer::CoreBlock) {

        slots_.bind_server(slot, proto::SlotServer::Session);
    }
}

[[nodiscard]] Ex<void> SlotBindings::apply_bind_slot(
    proto::SlotIndex slot,
    proto::LinkOp::SlotBind kind,
    proto::FileSize size,
    proto::PathId path_id,
    proto::IBlockRoles& roles
) {
    TASTY_SEAT_BODY(SlotBindings);
    return apply_bind_slot_(slot, kind, size, path_id, roles);
}

[[nodiscard]] Ex<SlotBindings::SaveBind> SlotBindings::bind_save_(
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
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), si.detail()});
    }

    if (!storage_life_) {
        save_src_->note_attached(si, false, proto::FileSize{});
        return SaveBind::Ready;
    }
    const auto backend = save_src_->backend(si);
    if (!backend)
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), si.detail()});

    if (!storage_life_->stage_backend(si, *backend, save_src_->order(si)))
        return SaveBind::Declined;

    proto::NoBlockRoles none;
    if (!slots_.submit_attach(si, none)) {
        ++tally_.stage_refusals;
        save_src_->note_attached(si, false, proto::FileSize{});
        return SaveBind::Ready;
    }
    save_src_->mark(si, Attach::Attaching);
    return SaveBind::Waiting;
}

proto::FileSize SlotBindings::save_extent_of_(proto::SlotIndex slot) const noexcept {
    return save_src_ == nullptr ? proto::FileSize{} : save_src_->size_of(slot);
}

void SlotBindings::begin_detach_(proto::SlotIndex si) noexcept {
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

    proto::NoBlockRoles none;
    if (!slots_.submit_detach(si, none)) {
        ++tally_.stage_refusals;
        return;
    }
    save_src_->mark(si, Attach::Detaching);
}

void SlotBindings::release_save_slots_() noexcept {
    for (unsigned i = 0; i < SaveImageSource::kSlots; ++i) {
        begin_detach_(proto::SlotIndex::of(i));
    }
}

ILinkEncoder::Outcome SlotBindings::perform_bound_mount_(
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
        r = core->mount(bm.index, cores::MountedPath{image->path.view(), image->size_bytes});
    } else {
        r = std::unexpected(Error{Errc::not_found, ERR_SITE(), bm.index.block_slot().detail()});
    }
    if (!r) {

        session_tally_.last_error = r.error();
        machine_.advise(Event::InfoRequest{.id = InfoId::ImageMountFailed}, advise_site_);
    }
    options_.publish_option_table(core);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SlotBindings::on(
    const proto::LinkOp::AttachSave& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    const bool pendable = SaveImageSource::holds(a.slot);
    OwedSaveBind& owed = owed_save_[pendable ? std::size_t{a.slot.detail()} : kOffSlot];
    return owed_save_bind_(owed, true, a.slot, a.act, a.path, a.manual, ctx.tx, ctx.core);
}

ILinkEncoder::Outcome SlotBindings::on(
    const proto::LinkOp::DetachSave& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(SlotBindings);
    const bool pendable = SaveImageSource::holds(a.slot);
    OwedSaveBind& owed = owed_save_[pendable ? std::size_t{a.slot.detail()} : kOffSlot];
    return owed_save_bind_(owed, false, a.slot, a.act, proto::FileId{}, false, ctx.tx, ctx.core);
}

ILinkEncoder::Outcome SlotBindings::owed_save_bind_(
    OwedSaveBind& owed,
    bool attach,
    proto::SlotIndex slot,
    proto::ActKey act,
    proto::FileId path,
    bool manual,
    const ILinkTxReader& inbox,
    infra::OptRef<cores::Core> core
) noexcept {

    owed.arm(act, save_body_(proto::FileSize{}));
    if (!attach) {
        begin_detach_(slot);

        const bool refused = save_src_ != nullptr
            && save_src_->attach_state(slot) == SaveImageSource::Attach::Attached;
        owed.answer(
            refused ? cores::SaveVerdict::Declined : cores::SaveVerdict::Known,
            save_body_(proto::FileSize{})
        );
        return ILinkEncoder::Outcome::Encoded;
    }
    const infra::OptRef<const InternedFile> file = inbox.file(path);
    if (!file) {
        owed.answer(cores::SaveVerdict::Declined, save_body_(proto::FileSize{}));
        return ILinkEncoder::Outcome::Dropped;
    }
    auto bound = bind_save_(slot, file->path.view(), manual, core);
    if (!bound) {
        owed.answer(cores::SaveVerdict::Declined, save_body_(proto::FileSize{}));
        return ILinkEncoder::Outcome::Dropped;
    }
    switch (*bound) {
        case SaveBind::Ready:
            owed.answer(
                cores::SaveVerdict::Known,
                save_body_(save_extent_of_(slot), save_src_ != nullptr && save_src_->exists(slot))
            );
            break;
        case SaveBind::Waiting:
            break;
        case SaveBind::Declined:
            owed.answer(cores::SaveVerdict::Declined, save_body_(proto::FileSize{}));
            break;
    }
    return ILinkEncoder::Outcome::Encoded;
}

void SlotBindings::settle_save_binds_() noexcept {
    if (save_src_ == nullptr)
        return;
    using Attach = SaveImageSource::Attach;
    for (unsigned i = 0; i < SaveImageSource::kSlots; ++i) {
        const proto::SlotIndex si = proto::SlotIndex::of(i);

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
            save_body_(
                known ? save_extent_of_(si) : proto::FileSize{},
                known && save_src_->exists(si)
            )
        );
    }
}

void SlotBindings::settle_stage_mount_(infra::OptRef<cores::Core> core) noexcept {
    if (!owed_mount_.owed())
        return;

    if (!owed_mount_.parked()) {
        if (prefetch_ && !prefetch_->park_acked()) {
            if (!owed_mount_.quiesce()->expired(clock())) {
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

cores::MountState SlotBindings::present_mount_(
    infra::OptRef<cores::Core> core,
    proto::SlotIndex slot,
    std::string_view path
) noexcept {
    if (!core.has_value())
        return cores::MountState::Failed;
    if (const auto sm = core->slot_mount())
        return sm->mount_slot(slot, path);
    const auto cd = core->staging_core();
    return !cd ? cores::MountState::Failed : cd->mount_disc(path);
}

void SlotBindings::after_mount_answer_(infra::OptRef<cores::Core> core) noexcept {
    options_.publish_option_table(core);
}

cores::MountStatus SlotBindings::mount_body_(infra::OptRef<cores::Core> core) noexcept {
    cores::MountStatus body{};
    const auto cd = core.has_value() ? core->staging_core() : infra::OptRef<cores::IStagingCore>{};
    if (cd)
        body.plan = cd->stage_plan();
    return body;
}

cores::SaveExtent SlotBindings::save_body_(proto::FileSize size_bytes, bool exists) noexcept {
    cores::SaveExtent body{};
    body.size_bytes = size_bytes;
    body.exists = exists;
    return body;
}

[[nodiscard]] Ex<void> SlotBindings::apply_bind_slot_(
    proto::SlotIndex slot,
    proto::LinkOp::SlotBind kind,
    proto::FileSize size,
    proto::PathId path_id,
    proto::IBlockRoles& roles
) {
    switch (kind) {
        case proto::LinkOp::SlotBind::Mount:
            return slots_.mount(slot, size, path_id, roles);
        case proto::LinkOp::SlotBind::Unmount:
            return slots_.unmount(slot);
        case proto::LinkOp::SlotBind::MountCd:
            return announce_disc_(slot, size, roles);
    }
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), proto::LinkOp::detail(kind)});
}

[[nodiscard]] Ex<void> SlotBindings::announce_disc_(
    proto::SlotIndex slot,
    proto::FileSize size,
    proto::IBlockRoles& roles
) {
    proto::SpiFrame frame{link_};
    return announce_.mount_disc(slots_, frame, slot, size, roles);
}

void SlotBindings::mount_save_channel(
    cores::ISaveChannel& channel,
    proto::BlockSlotMask poisoned,
    bool remounts_saves
) {
    TASTY_SEAT_BODY(SlotBindings);
    if (!vfs_ || !remounts_saves)
        return;
    const auto plan = channel.mount_plan();
    if (!plan)
        return;

    SaveChannelRoles roles{channel};
    const proto::BlockSlotMask previous = channel_slots_;
    unmount_save_channel();

    for (const auto& m : plan->mounts()) {
        if (!m)
            session_tally_.last_error = m.error();
    }
    release_channel_slots_(previous, plan->claimed());

    using Part = cores::ISaveChannel::OrderPart;
    Part part = Part::RowAndPlan;
    for (const auto& m : plan->mounts()) {
        if (!m)
            continue;
        const proto::SlotIndex si = m->slot();

        if (slots_.server(si) == proto::SlotServer::CoreBlock) {
            ++tally_.save_mount_collisions;
            session_tally_.last_error = Error{Errc::busy, ERR_SITE(), si.detail()};
            continue;
        }

        if (poisoned.has(si)) {
            ++tally_.stage_refusals;
            session_tally_.last_error = Error{Errc::would_block, ERR_SITE(), si.detail()};
            continue;
        }
        if (!storage_life_) {
            channel.descriptor().note_attached(si, false, proto::FileSize{});
        } else {
            const bool staged = storage_life_->stage_backend(
                si,
                channel.backend(),
                channel.compose_order(m->row, part)
            );
            if (staged)
                channel_borrows_ = channel_borrows_.with(si);
            if (!staged || !slots_.submit_attach(si, roles)) {
                ++tally_.stage_refusals;
                channel.descriptor().note_attached(si, false, proto::FileSize{});
            } else {
                part = Part::RowOnly;
            }
        }

        slots_.bind_server(si, proto::SlotServer::CoreDescriptor);
        if (auto r = apply_bind_slot_(
                si,
                proto::LinkOp::SlotBind::Mount,
                m->size,
                proto::PathId{0},
                roles
            );
            !r) {
            slots_.bind_server(si, proto::SlotServer::Session);
            session_tally_.last_error = r.error();
            continue;
        }
        channel_slots_ = channel_slots_.with(si);
        log_lane_.push(LogRec::Mount{si}, log_stamp(log_now()));
    }
}

void SlotBindings::unmount_save_channel() {
    proto::NoBlockRoles none;
    for (std::uint8_t i = 0; i < proto::kBlockSlots; ++i) {
        const proto::SlotIndex si{i};
        if (!channel_slots_.has(si))
            continue;
        slots_.bind_server(si, proto::SlotServer::Session);
        if (auto r = apply_bind_slot_(
                si,
                proto::LinkOp::SlotBind::Unmount,
                proto::FileSize{},
                proto::PathId{},
                none
            );
            r)
            log_lane_.push(LogRec::Unmount{si}, log_stamp(log_now()));
    }
    channel_slots_ = {};
}

void SlotBindings::release_channel_slots_(
    proto::BlockSlotMask mask,
    proto::BlockSlotMask kept
) noexcept {

    proto::NoBlockRoles none;
    (void)slots_.install_completions(none);
    for (std::uint8_t i = 0; i < proto::kBlockSlots; ++i) {
        const proto::SlotIndex si = proto::SlotIndex::of(i);
        if (!mask.has(si))
            continue;
        if (!kept.has(si) && storage_life_) {
            if (!slots_.submit_detach(si, none))
                ++tally_.stage_refusals;
            continue;
        }
        if (!storage_life_)
            continue;
        if (storage_life_->release_slot(si)) {
            channel_borrows_ = channel_borrows_.without(si);
        } else {
            ++tally_.channel_release_refusals;
        }
    }
}

}
