// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/held_transfer.h"

#include "app/delivered.h"

namespace mister::app {

HeldTransfer::HeldTransfer(const Wiring& w) noexcept
    : link_(w.link),
      session_(w.session),
      binder_(w.binder),
      machine_(w.machine),
      advise_site_(w.advise_site),
      bindings_(w.bindings),
      cheats_(w.cheats),
      osd_(w.osd),
      slot_bindings_(w.slot_bindings),
      tenancy_(w.tenancy),
      session_tally_(w.session_tally),
      window_jobs_(w.window_jobs),
      frames_(w.frames) {}

Delivered HeldTransfer::on(const proto::LinkOp::FileTx& a, const LinkOpCtx& ctx) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    using Phase = proto::LinkOp::FileTxPhase;

    const bool spends = a.phase != Phase::Whole && a.phase != Phase::Empty;

    const bool window = a.phase == Phase::WindowOpen || a.phase == Phase::WindowClose;

    const bool role = a.phase == Phase::BeforeClose
        ? off_link_.has_value()
        : ctx.core.has_value() && (window ? ctx.core->window_load() : ctx.core->stream_load());
    if (!ctx.core.has_value()
        || machine_.state() == SessionState::Failed
        || (!role && a.phase != Phase::Whole)) {
        if (a.phase != Phase::Whole)
            ++tally_.file_tx_piece_drops;

        if (a.phase == Phase::WindowOpen)
            publish_window_level(a.act, false);
        if (!window)
            publish_stream(a.act, false);
        return Delivered{ILinkEncoder::Outcome::Dropped, spends};
    }

    const bool closes = a.phase == Phase::Close
        || a.phase == Phase::WindowClose
        || a.phase == Phase::BeforeClose;
    const bool opens_off_link = (a.phase == Phase::Open || a.phase == Phase::WindowOpen)
        && !link_carries(downloads_);
    if (opens_off_link || (closes && off_link_.has_value()))
        return off_link_file_tx_(a, ctx);
    const infra::OptRef<const InternedFile> file = ctx.tx.file(a.file);
    cut_stage_();
    if (a.phase == Phase::Whole || a.phase == Phase::Close || a.phase == Phase::Empty)
        arm_open_save_(file, ctx.tx);

    const std::optional<Delivered::Edge> answered = (window || ctx.core->stream_load())
        ? stream_file_tx_(a, file, *ctx.core)
        : std::nullopt;
    const bool ok = answered.has_value();
    const Delivered::Edge edge = answered.value_or(Delivered::Edge::None);
    open_save_.reset();
    if (!window && !(ok && link_.posted()))
        publish_stream(a.act, ok);
    if (!ok)
        return Delivered{ILinkEncoder::Outcome::Dropped, spends};
    return Delivered{ILinkEncoder::Outcome::Encoded, spends, edge};
}

Delivered HeldTransfer::on(const proto::LinkOp::StagePayload& a, const LinkOpCtx& ctx) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    using Phase = proto::LinkOp::StagePhase;

    const bool off_link = !link_carries(downloads_)
        && a.copy_word == 0
        && (a.phase == Phase::Open || a.phase == Phase::Close);
    const ILinkEncoder::Outcome r = off_link ? off_link_stage_payload_(a, ctx)
                                             : stage_payload_(a, ctx);
    echo_payload_act_(a, r != ILinkEncoder::Outcome::Dropped);

    return Delivered{r, !off_link && a.payload != proto::FileId{} && a.copy_word == 0};
}

Delivered HeldTransfer::on(const proto::LinkOp::PlainDownload& a, const LinkOpCtx& ctx) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    using Phase = proto::LinkOp::StagePhase;

    if (dat_window_held() && !keeps_window(a))
        release_window();
    const bool opens = a.phase == Phase::Whole || a.phase == Phase::Open;
    const bool closes = a.phase == Phase::Whole || a.phase == Phase::Close;
    const bool spends = a.payload != proto::FileId{};
    infra::OptRef<const InternedFile> file{};
    std::span<const std::uint8_t> bytes{};
    if (a.payload != proto::FileId{}) {
        file = ctx.tx.file(a.payload);
        if (!file)
            return Delivered{drop_plain_(), spends};
        bytes = file->bytes;
    }
    if (opens) {
        cut_stage_();
        cut_file_tx_(ctx.core);

        if (binder_.fio_queue().held() || binder_.fio_queue().tenant_holds_fio()) {
            return Delivered{drop_plain_(), spends};
        }
        (void)binder_.fio_queue().flush();
        auto ds = proto::link::DownloadSession::begin(link_, a.dest);
        if (!ds)
            return Delivered{ILinkEncoder::Outcome::Dropped, spends};
        plain_ds_.emplace(std::move(*ds));

        binder_.fio_queue().hold();
        if (auto r = plain_ds_->open_dat(a.framing); !r)
            return Delivered{drop_plain_(), spends};
    } else if (!plain_ds_) {
        return Delivered{drop_plain_(), spends};
    }

    if (a.phase == Phase::Close && !file) {
        end_plain_();
        ++tally_.stage_cuts;
        return Delivered{ILinkEncoder::Outcome::Encoded, false};
    }
    if (!file)
        return Delivered{drop_plain_(), spends};

    if (hal::words(a.framing, bytes.size()) > proto::link::tightest_load_budget_words()) {
        return Delivered{drop_plain_(), spends};
    }

    if (!plain_ds_->dat_held()) {
        if (auto r = plain_ds_->open_dat(a.framing); !r)
            return Delivered{drop_plain_(), spends};
    }
    if (auto r = plain_ds_->feed(bytes); !r)
        return Delivered{drop_plain_(), spends};
    if (closes)
        end_plain_();
    return Delivered{ILinkEncoder::Outcome::Encoded, spends};
}

bool HeldTransfer::keeps_window(const proto::LinkOp::PlainDownload& a) const noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    if (!plain_ds_)
        return false;
    using Phase = proto::LinkOp::StagePhase;
    if (a.phase != Phase::Piece && a.phase != Phase::Close)
        return false;
    return a.dest == plain_ds_->index();
}

void HeldTransfer::release_window() noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    if (plain_ds_)
        plain_ds_->release_dat();
}

void HeldTransfer::echo_payload_act_(const proto::LinkOp::StagePayload& a, bool encoded) noexcept {
    if (a.act && !copy_posted() && !length_act_)
        publish_payload_echo(a.act.widen(), encoded);
}

ILinkEncoder::Outcome HeldTransfer::stage_payload_(
    const proto::LinkOp::StagePayload& a,
    const LinkOpCtx& ctx
) noexcept {
    using Phase = proto::LinkOp::StagePhase;
    const bool opens = a.phase == Phase::Whole || a.phase == Phase::Open;
    const bool closes = a.phase == Phase::Whole || a.phase == Phase::Close;
    const bool has_staging = ctx.core.has_value() && ctx.core->staging_core();

    if (!ctx.core.has_value() || (!has_staging && proto::sets_region(a))) {
        return drop_stage_();
    }
    infra::OptRef<const InternedFile> file{};
    std::span<const std::uint8_t> bytes{};
    if (a.payload != proto::FileId{}) {
        file = ctx.tx.file(a.payload);
        if (!file)
            return drop_stage_();
        bytes = file->bytes;
    }

    const bool load_phase = a.phase == Phase::Whole && ctx.starting;
    if (!load_phase && !stage_fits_round_(bytes.size()))
        return drop_stage_();
    if (a.copy_word != 0) {
        return a.phase == Phase::Whole ? stage_copy_(a, bytes) : drop_stage_();
    }
    if (a.phase == Phase::Whole && file && bytes.empty() && file->aux != 0) {
        return stage_length_(a, *file);
    }

    if (a.phase == Phase::Close && !file) {
        if (!stage_open_)
            return drop_stage_();
        cut_stage_();
        return ILinkEncoder::Outcome::Encoded;
    }
    if (opens) {
        cut_stage_();
        cut_file_tx_(ctx.core);
        proto::SessionParams params{};
        if (file) {
            params.ext = file->ext.view();
            params.aux = proto::AuxValue{file->aux};
        }
        auto ds = proto::ImageBracket::open(ctx.core->image_sink(), a.dest, params);
        if (!ds)
            return ILinkEncoder::Outcome::Dropped;
        stage_open_.emplace(std::move(*ds));
        stage_tick_.bind(
            a.progress ? infra::OptRef<cores::ILoadProgress>{*this}
                       : infra::OptRef<cores::ILoadProgress>{}
        );
        stage_tick_.begin(file ? file->size_bytes : proto::FileSize{});
    } else if (!stage_open_) {
        return drop_stage_();
    }
    if (!bytes.empty() && !drain_payload_(bytes, a.chunk, *file)) {
        cut_stage_();
        return ILinkEncoder::Outcome::Dropped;
    }
    if (!closes)
        return ILinkEncoder::Outcome::Encoded;
    stage_tick_.finish();
    const bool ended = stage_open_->end().has_value();
    stage_open_.reset();
    if (!ended)
        return ILinkEncoder::Outcome::Dropped;

    if (proto::sets_region(a)) {
        if (const auto cd = ctx.core->staging_core()) {
            cd->set_region(a.region);
        }
    }
    return ILinkEncoder::Outcome::Encoded;
}

Delivered HeldTransfer::on(
    const proto::LinkOp::StageDiscPayload& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    const auto cd = ctx.core.has_value() ? ctx.core->staging_core()
                                         : infra::OptRef<cores::IStagingCore>{};
    if (!cd)
        return Delivered{ILinkEncoder::Outcome::Dropped, false};
    if (auto r = cd->stage_disc_payload(a); !r) {
        return Delivered{ILinkEncoder::Outcome::Dropped, false};
    }
    return Delivered{ILinkEncoder::Outcome::Encoded, false};
}

Delivered HeldTransfer::on(const proto::LinkOp::StageReset&, const LinkOpCtx& ctx) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    const auto cd = ctx.core.has_value() ? ctx.core->staging_core()
                                         : infra::OptRef<cores::IStagingCore>{};
    if (!cd)
        return Delivered{ILinkEncoder::Outcome::Dropped, false};
    if (auto r = cd->deferred_reset(); !r)
        return Delivered{ILinkEncoder::Outcome::Dropped, false};
    return Delivered{ILinkEncoder::Outcome::Encoded, false};
}

Delivered HeldTransfer::on(const proto::LinkOp::SetWideIndex& a, const LinkOpCtx& ctx) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    const auto cd = ctx.core.has_value() ? ctx.core->staging_core()
                                         : infra::OptRef<cores::IStagingCore>{};

    if (!cd) {
        return Delivered{ILinkEncoder::Outcome::Dropped, false};
    }
    (void)binder_.fio_queue().flush();
    proto::link::SpiFrame frame{link_};
    if (auto r = proto::link::DownloadSession::set_index(frame, a.dest); !r) {
        return Delivered{ILinkEncoder::Outcome::Dropped, false};
    }
    return Delivered{ILinkEncoder::Outcome::Encoded, false};
}

Delivered HeldTransfer::misrouted(const proto::LinkOp&, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    ++tally_.link_op_misrouted;
    return Delivered{ILinkEncoder::Outcome::Dropped, false};
}

void HeldTransfer::arm_open_save_(
    infra::OptRef<const InternedFile> file,
    const ILinkTxReader& inbox
) noexcept {
    open_save_.reset();
    if (!file || file->save == proto::FileId{})
        return;
    const infra::OptRef<const InternedFile> save = inbox.file(file->save);
    if (!save) {
        ++tally_.open_save_refusals;
        return;
    }
    open_save_ = OpenSave{.path = file->save.path(), .size = proto::FileSize{save->size_bytes}};
}

void HeldTransfer::announce_open_save(proto::IBlockRoles& roles) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    if (!open_save_)
        return;
    const OpenSave s = *open_save_;
    open_save_.reset();
    if (!slot_bindings_.apply_bind_slot(
            proto::SlotIndex{0},
            proto::LinkOp::SlotBind::Mount,
            s.size,
            s.path,
            roles
        )) {
        ++tally_.open_save_refusals;
        return;
    }
    ++tally_.open_saves_mounted;
}

void HeldTransfer::before_close(bool remounts_saves) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    proto::NoBlockRoles none;
    const infra::OptRef<cores::Core> core = tenancy_.core();
    std::optional<CoreBlockRoles> core_roles;
    if (core)
        core_roles.emplace(*core);
    proto::IBlockRoles& roles = core_roles.has_value()
        ? static_cast<proto::IBlockRoles&>(*core_roles)
        : none;
    announce_open_save(roles);
    if (!remounts_saves || !core)
        return;
    if (const auto channel = core->save_channel())
        slot_bindings_.mount_save_channel(*channel, tenancy_.poisoned_slots(), remounts_saves);
}

std::optional<Delivered::Edge> HeldTransfer::stream_file_tx_(
    const proto::LinkOp::FileTx& a,
    infra::OptRef<const InternedFile> file,
    cores::Core& current
) {
    using Phase = proto::LinkOp::FileTxPhase;

    const infra::OptRef<cores::Core> here{current};

    const bool window = a.phase == Phase::WindowOpen || a.phase == Phase::WindowClose;
    const bool opens = a.phase == Phase::Whole
        || a.phase == Phase::Open
        || a.phase == Phase::WindowOpen
        || a.phase == Phase::Empty;
    const bool closes = a.phase == Phase::Whole
        || a.phase == Phase::Close
        || a.phase == Phase::WindowClose
        || a.phase == Phase::Empty;

    if (opens) {
        if (!open_stream_bracket_(a, file, current))
            return std::nullopt;
        if (window)
            return Delivered::Edge::None;
    } else if (!ftx_open_) {
        ++tally_.file_tx_piece_drops;
        return std::nullopt;
    }

    if ((a.phase == Phase::Close || a.phase == Phase::WindowClose) && !file) {
        cut_file_tx_(here);
        return Delivered::Edge::None;
    }
    if (!window && a.phase != Phase::Empty) {

        if (!file) {
            ++tally_.file_tx_piece_drops;
            cut_file_tx_(here);
            return std::nullopt;
        }
        const auto taken = ftx_open_->write_taken(file->bytes);
        if (!taken) {
            ++tally_.file_tx_piece_drops;
            cut_file_tx_(here);
            return std::nullopt;
        }

        if (link_.posted()) {
            ++tally_.file_tx_holds;
            open_save_.reset();
            publish_stream(a.act, true, static_cast<std::uint32_t>(taken->size()));
            return Delivered::Edge::None;
        }
    }
    if (!closes)
        return Delivered::Edge::None;
    return close_stream_bracket_(a, *file, current);
}

bool HeldTransfer::open_stream_bracket_(
    const proto::LinkOp::FileTx& a,
    infra::OptRef<const InternedFile> file,
    cores::Core& current
) {
    using Phase = proto::LinkOp::FileTxPhase;
    const bool window = a.phase == Phase::WindowOpen || a.phase == Phase::WindowClose;
    if (ftx_open_)
        cut_file_tx_(infra::OptRef<cores::Core>{current});
    if (!file || (!window && a.phase != Phase::Empty && file->bytes.empty())) {
        if (a.phase != Phase::Whole)
            ++tally_.file_tx_piece_drops;
        if (window)
            publish_window_level(a.act, false);
        return false;
    }

    if (!window) {
        binder_.file_progress().arm(*this, a.phase == Phase::Whole ? file->bytes.size() : a.total);
    }
    const auto load = window ? current.window_load() : current.stream_load();
    if (!load) {
        refuse_open_(window, a.act);
        return false;
    }

    const cores::StreamAsk ask{.index = a.wire_index, .path = file->path.view()};
    auto b = cores::StreamLoadBracket::open(
        *load,
        current.image_sink(),
        ask,
        window ? cores::StreamLoadKind::Window : cores::StreamLoadKind::Stream,
        window ? a.total : 0
    );
    if (!b) {
        refuse_open_(window, a.act);
        return false;
    }
    ftx_open_.emplace(std::move(*b));
    if (!window)
        return true;

    const auto role = current.window_save();
    if (role && window_jobs_ && window_jobs_->hold_open(*role, a.wire_index.io(), a.act)) {
        return true;
    }
    publish_window_level(a.act, true);
    return true;
}

std::optional<Delivered::Edge> HeldTransfer::close_stream_bracket_(
    const proto::LinkOp::FileTx& a,
    const InternedFile& file,
    cores::Core& current
) {
    using Phase = proto::LinkOp::FileTxPhase;

    const bool window = a.phase == Phase::WindowClose;

    before_close(file.remounts_saves);
    const std::uint64_t total = a.phase == Phase::Whole ? file.bytes.size() : a.total;
    const bool ok = ftx_open_
                        ->close(
                            live_load_(current, ftx_open_->kind()),
                            proto::FileSize{total},
                            file.crc
                        )
                        .has_value();
    ftx_open_.reset();
    if (!window)
        binder_.file_progress().disarm();
    if (!ok)
        return std::nullopt;

    if (file.edge != ContentEdge::Companion)
        return after_file_tx(a.wire_index, file.path.view(), file.edge, current);
    return Delivered::Edge::None;
}

void HeldTransfer::refuse_open_(bool window, proto::ActKey act) noexcept {
    if (window) {
        publish_window_level(act, false);
    } else {
        binder_.file_progress().disarm();
    }
}

Delivered::Edge HeldTransfer::after_file_tx(
    proto::FioIndexByte index,
    std::string_view path,
    ContentEdge edge,
    cores::Core& current
) {
    TASTY_SEAT_BODY(HeldTransfer);
    ++tally_.downloads_closed;
    last_download_ = index;
    last_tx_bytes_ = proto::FileSize{current.last_tx_bytes()};

    if (bindings_.declares_cheats() && edge != ContentEdge::Silent) {
        TxDigest loaded{};
        loaded.gen = bindings_.generation();
        loaded.kind = TxDigest::Kind::Load;
        loaded.crc = current.last_tx_crc();
        (void)loaded.path.assign(edge == ContentEdge::ChainEnd ? std::string_view{} : path);
        cheats_.content_loaded(current, loaded);
    }
    machine_.advise(Event::SdActivity{.index = index}, advise_site_);
    return Delivered::Edge::DownloadClosed;
}

void HeldTransfer::publish_window_level(proto::ActKey act, bool open) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    ftx_level_scratch_.act = act;
    ftx_level_scratch_.open = open;
    ftx_level_scratch_.payload = false;

    if (open && off_link_) {
        ftx_level_scratch_.params = off_link_->params();
        hold_off_link_();
    }
    ftx_level_cell_.publish(ftx_level_scratch_);
}

void HeldTransfer::publish_payload_echo(proto::ActKey act, bool encoded) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    ftx_level_scratch_.act = act;
    ftx_level_scratch_.open = encoded;
    ftx_level_scratch_.payload = true;
    ftx_level_cell_.publish(ftx_level_scratch_);
}

void HeldTransfer::publish_stream(
    proto::ActKey act,
    bool served,
    std::optional<std::uint32_t> held
) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    if (!act)
        return;
    ftx_level_scratch_.stream_act = act;
    ftx_level_scratch_.stream_served = served;
    if (held) {
        ftx_level_scratch_.held_act = act;
        ftx_level_scratch_.held_bytes = *held;
    }
    ftx_level_cell_.publish(ftx_level_scratch_);
}

bool HeldTransfer::save_pull_fenced(infra::OptRef<cores::Core> current) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    if ((!ftx_open_ && !stage_open_ && !plain_ds_ && !off_link_open_())
        || !current->save_upload()) {
        return false;
    }
    ++tally_.save_pulls_fenced;
    return true;
}

infra::OptRef<cores::IStreamLoad> HeldTransfer::live_load_(
    cores::Core& core,
    cores::StreamLoadKind kind
) noexcept {
    return kind == cores::StreamLoadKind::Window ? core.window_load() : core.stream_load();
}

void HeldTransfer::cut_file_tx_(infra::OptRef<cores::Core> current) noexcept {
    if (!ftx_open_)
        return;

    if (current)
        ftx_open_->cut(live_load_(*current, ftx_open_->kind()));
    ftx_open_.reset();
    ++tally_.file_tx_cuts;
    binder_.file_progress().disarm();
}

Delivered HeldTransfer::off_link_file_tx_(const proto::LinkOp::FileTx& a, const LinkOpCtx& ctx) {
    using Phase = proto::LinkOp::FileTxPhase;
    const infra::OptRef<const InternedFile> file = ctx.tx.file(a.file);
    if (a.phase == Phase::Open || a.phase == Phase::WindowOpen)
        return off_link_open_window_(a, file, *ctx.core);
    if (a.phase != Phase::BeforeClose)
        return off_link_close_(a, file, *ctx.core);

    arm_open_save_(file, ctx.tx);
    before_close(file.has_value() && file->remounts_saves);
    open_save_.reset();
    publish_stream(a.act, true);
    return Delivered{ILinkEncoder::Outcome::Encoded, false};
}

bool HeldTransfer::off_link_takes_queue_(infra::OptRef<cores::Core> current) noexcept {
    cut_stage_();
    cut_file_tx_(current);
    end_plain_();
    end_off_link_(current, true);

    if (binder_.fio_queue().held() || binder_.fio_queue().tenant_holds_fio())
        return false;
    (void)binder_.fio_queue().flush();
    return true;
}

Delivered HeldTransfer::off_link_open_window_(
    const proto::LinkOp::FileTx& a,
    infra::OptRef<const InternedFile> file,
    cores::Core& current
) {
    using Phase = proto::LinkOp::FileTxPhase;
    const bool window = a.phase == Phase::WindowOpen;
    const infra::OptRef<cores::Core> here{current};
    if (!file || !off_link_takes_queue_(here))
        return off_link_refuse_(window, a.act);
    const auto load = window ? current.window_load() : current.stream_load();
    if (!load)
        return off_link_refuse_(window, a.act);
    const cores::StreamAsk ask{.index = a.wire_index, .path = file->path.view()};
    auto w = cores::OffLinkWindow::open(
        *load,
        ask,
        window ? cores::StreamLoadKind::Window : cores::StreamLoadKind::Stream,
        window ? proto::AuxValue{a.total} : proto::AuxValue{}
    );
    if (!w)
        return off_link_refuse_(window, a.act);
    binder_.fio_queue().hold();
    off_link_.emplace(std::move(*w));
    tally_.off_link_windows.add();
    if (window) {

        const auto role = current.window_save();
        if (!(role && window_jobs_ && window_jobs_->hold_open(*role, a.wire_index.io(), a.act))) {
            publish_window_level(a.act, true);
        }
        return Delivered{ILinkEncoder::Outcome::Encoded, false};
    }
    ftx_level_scratch_.params = off_link_->params();
    hold_off_link_();
    publish_stream(a.act, true);
    return Delivered{ILinkEncoder::Outcome::Encoded, false};
}

Delivered HeldTransfer::off_link_refuse_(bool window, proto::ActKey act) {
    ++tally_.file_tx_piece_drops;
    tally_.off_link_refusals.add();
    if (window) {
        publish_window_level(act, false);
    } else {
        publish_stream(act, false);
    }
    return Delivered{ILinkEncoder::Outcome::Dropped, false};
}

Delivered HeldTransfer::off_link_close_(
    const proto::LinkOp::FileTx& a,
    infra::OptRef<const InternedFile> file,
    cores::Core& current
) {
    using Phase = proto::LinkOp::FileTxPhase;
    const bool window = a.phase == Phase::WindowClose;
    const infra::OptRef<cores::Core> here{current};
    if (!off_link_) {
        ++tally_.file_tx_piece_drops;
        if (!window)
            publish_stream(a.act, false);
        return Delivered{ILinkEncoder::Outcome::Dropped, false};
    }

    if (!file) {
        end_off_link_(here, true);
        ++tally_.file_tx_cuts;
        if (!window)
            publish_stream(a.act, true);
        return Delivered{ILinkEncoder::Outcome::Encoded, false};
    }
    off_link_->close(live_load_(current, off_link_->kind()), proto::FileSize{a.total}, file->crc);
    end_off_link_(here, false);

    const Delivered::Edge edge = file->edge != ContentEdge::Companion
        ? after_file_tx(a.wire_index, file->path.view(), file->edge, current)
        : Delivered::Edge::None;
    if (!window)
        publish_stream(a.act, true);
    return Delivered{ILinkEncoder::Outcome::Encoded, false, edge};
}

ILinkEncoder::Outcome HeldTransfer::off_link_stage_payload_(
    const proto::LinkOp::StagePayload& a,
    const LinkOpCtx& ctx
) noexcept {
    using Phase = proto::LinkOp::StagePhase;
    const bool has_staging = ctx.core.has_value() && ctx.core->staging_core();
    if (!ctx.core.has_value() || (!has_staging && proto::sets_region(a)))
        return drop_stage_();
    if (a.phase == Phase::Open) {

        const infra::OptRef<const InternedFile> file = ctx.tx.file(a.payload);
        if (!file || !off_link_takes_queue_(ctx.core)) {
            tally_.off_link_refusals.add();
            return drop_stage_();
        }
        binder_.fio_queue().hold();
        const proto::SessionParams own{.aux = proto::AuxValue{file->aux}, .ext = file->ext.view()};
        ftx_level_scratch_.params = proto::link::FioOpen::of(a.dest, own);
        off_link_stage_ = a.dest;
        hold_off_link_();
        tally_.off_link_windows.add();
        return ILinkEncoder::Outcome::Encoded;
    }
    if (!off_link_stage_)
        return drop_stage_();

    const bool cut = a.payload == proto::FileId{};
    end_off_link_(ctx.core, cut);
    if (cut) {
        ++tally_.stage_cuts;
        return ILinkEncoder::Outcome::Encoded;
    }
    if (proto::sets_region(a)) {
        if (const auto cd = ctx.core->staging_core())
            cd->set_region(a.region);
    }
    return ILinkEncoder::Outcome::Encoded;
}

void HeldTransfer::end_off_link_(infra::OptRef<cores::Core> current, bool cut) noexcept {
    if (!off_link_open_())
        return;
    if (cut && off_link_ && current)
        off_link_->cut(live_load_(*current, off_link_->kind()));
    if (cut)
        tally_.off_link_cuts.add();
    off_link_.reset();
    off_link_stage_.reset();
    off_link_holds_ = false;

    if (frames_)
        frames_->resume();
    binder_.fio_queue().release();
}

void HeldTransfer::hold_off_link_() noexcept {
    off_link_holds_ = true;
    if (frames_)
        frames_->suspend();
}

ILinkEncoder::Outcome HeldTransfer::stage_copy_(
    const proto::LinkOp::StagePayload& a,
    std::span<const std::uint8_t> bytes
) noexcept {
    cut_stage_();
    const std::size_t n = bytes.size() / sizeof(hal::LinkWord);
    if (copy_ds_ || proto::sets_region(a) || a.copy_word > n || n > kCopyWordsMax) {
        return ILinkEncoder::Outcome::Dropped;
    }
    std::array<hal::LinkWord, kCopyWordsMax> w{};

    for (std::size_t i = 0; i < n; ++i) {
        w[i] = hal::LinkWord::load_le(
            bytes.subspan(i * sizeof(hal::LinkWord)).first<sizeof(hal::LinkWord)>()
        );
    }
    (void)binder_.fio_queue().flush();
    auto ds = proto::link::DownloadSession::begin(link_, a.dest);
    if (!ds)
        return ILinkEncoder::Outcome::Dropped;
    if (auto r = ds->post(std::span(w).first(a.copy_word)); !r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    copy_tail_n_ = static_cast<std::uint8_t>(n - a.copy_word);
    for (std::size_t i = 0; i < copy_tail_n_; ++i)
        copy_tail_[i] = w[a.copy_word + i];
    copy_act_ = a.act.widen();
    copy_ds_.emplace(std::move(*ds));
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome HeldTransfer::stage_length_(
    const proto::LinkOp::StagePayload& a,
    const InternedFile& file
) noexcept {
    cut_stage_();
    if (copy_ds_ || binder_.fio_queue().held() || binder_.fio_queue().tenant_holds_fio()) {
        return ILinkEncoder::Outcome::Dropped;
    }
    (void)binder_.fio_queue().flush();
    proto::SessionParams params{.aux = proto::AuxValue{file.aux}, .ext = file.ext.view()};
    auto ds = proto::link::DownloadSession::begin(link_, a.dest, params);
    if (!ds)
        return ILinkEncoder::Outcome::Dropped;
    const auto closed = ds->end_or_post();
    if (!closed)
        return ILinkEncoder::Outcome::Dropped;
    if (!*closed)
        length_act_ = a.act.widen();
    return ILinkEncoder::Outcome::Encoded;
}

void HeldTransfer::end_length_(bool ok) noexcept {
    if (!length_act_)
        return;
    publish_payload_echo(length_act_, ok);
    if (!ok)
        ++session_tally_.link_op_drops;
    length_act_ = {};
}

bool HeldTransfer::hold_wire(bool park_asked) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    if (!wire_held())
        return false;

    if (session_.configure_posted())
        return true;
    if (plain_ds_ && plain_ds_->dat_held()) {

        if (park_asked) {
            end_plain_();
            ++tally_.file_tx_hold_abandons;
            return false;
        }
        ++tally_.file_tx_held_rounds;
        return true;
    }

    if (!copy_ds_ && !link_.posted()) {
        tally_.off_link_rounds.add();
        return true;
    }
    if (!copy_ds_) {

        if (park_asked) {
            link_.abandon_post();
            ++tally_.file_tx_hold_abandons;
            end_length_(false);
            return off_link_holds_;
        }
        const auto done = link_.posted_done();
        if (done && !*done) {
            ++tally_.file_tx_held_rounds;
            return true;
        }
        end_length_(done.has_value());
        return off_link_holds_;
    }
    if (park_asked) {
        copy_ds_->abandon();
        end_copy_(false);
        return false;
    }
    const auto done = copy_ds_->settle(std::span(copy_tail_).first(copy_tail_n_));
    if (done && !*done) {
        ++tally_.copy_held_rounds;
        return true;
    }
    const bool ok = done.has_value() && copy_ds_->end().has_value();
    end_copy_(ok);
    return false;
}

void HeldTransfer::end_copy_(bool ok) noexcept {
    copy_ds_.reset();
    if (copy_act_)
        publish_payload_echo(copy_act_, ok);
    if (!ok)
        ++session_tally_.link_op_drops;
    copy_act_ = {};
    copy_tail_n_ = 0;
}

void HeldTransfer::cut_all() noexcept {
    TASTY_SEAT_BODY(HeldTransfer);

    if (copy_ds_) {
        copy_ds_->abandon();
        end_copy_(false);
    }
    if (owns_post_()) {
        link_.abandon_post();
        ++tally_.file_tx_hold_abandons;
    }
    end_length_(false);

    cut_file_tx_(tenancy_.core());
    cut_stage_();
    end_plain_();
    end_off_link_(tenancy_.core(), true);
    if (window_jobs_)
        window_jobs_->forget_held();
}

bool HeldTransfer::drain_payload_(
    std::span<const std::uint8_t> bytes,
    std::uint16_t chunk_bytes,
    const InternedFile& from
) noexcept {
    const auto chunk = static_cast<std::size_t>(chunk_bytes);
    if (chunk == 0)
        return stage_open_->write(bytes).has_value();
    std::size_t off = 0;
    while (off < bytes.size()) {
        const std::size_t n = std::min(chunk, bytes.size() - off);
        if (auto w = stage_open_->write(bytes.subspan(off, n)); !w)
            return false;
        off += n;
        stage_tick_.advance(from.offset - proto::kFileStart + proto::FileSize{off});
    }
    return true;
}

ILinkEncoder::Outcome HeldTransfer::drop_stage_() noexcept {
    ++tally_.stage_drops;
    cut_stage_();
    return ILinkEncoder::Outcome::Dropped;
}

bool HeldTransfer::stage_fits_round_(std::size_t bytes) const noexcept {
    const std::size_t words = hal::words(link_.width(), bytes);
    return words <= proto::link::tightest_load_budget_words();
}

ILinkEncoder::Outcome HeldTransfer::drop_plain_() noexcept {
    ++tally_.stage_drops;
    end_plain_();
    return ILinkEncoder::Outcome::Dropped;
}

void HeldTransfer::end_plain_() noexcept {
    if (!plain_ds_)
        return;
    plain_ds_->release_dat();
    (void)plain_ds_->end();
    plain_ds_.reset();
    binder_.fio_queue().release();
}

void HeldTransfer::cut_stage_() noexcept {
    if (!stage_open_)
        return;
    stage_open_.reset();
    ++tally_.stage_cuts;
    stage_tick_.finish();
}

void HeldTransfer::on_load_progress(cores::Progress progress) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);

    machine_.advise(Event::ProgressUpdate{.progress = progress}, advise_site_);

    if (link_.posted())
        return;

    const bool done = progress.dismisses();

    if (!done && !osd_.opened_by(OsdWire::ShowOwner::Bar) && !osd_.lit()) {
        (void)osd_.show_as(proto::LinkOp::OsdShow::Overlay, OsdWire::ShowOwner::Bar);
    } else if (done) {
        (void)osd_.release(OsdWire::ShowOwner::Bar);
    }

    for (proto::OsdRow r{}; proto::OsdSurface::kMaxRows.covers(r) && osd_.flush_one();
         r += proto::kOneOsdRow) {
    }
}

Delivered HeldTransfer::on(const proto::LinkOp::FileTx& a, const LinkOpCtx&, AnswerOnly) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    using Phase = proto::LinkOp::FileTxPhase;

    if (a.phase == Phase::WindowOpen) {
        publish_window_level(a.act, false);
    } else if (a.phase != Phase::WindowClose) {
        publish_stream(a.act, false);
    }

    return Delivered{
        ILinkEncoder::Outcome::Dropped,
        a.phase != Phase::Whole && a.phase != Phase::Empty
    };
}

Delivered HeldTransfer::on(
    const proto::LinkOp::StagePayload& a,
    const LinkOpCtx&,
    AnswerOnly
) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);

    echo_payload_act_(a, false);
    return Delivered{
        ILinkEncoder::Outcome::Dropped,
        a.payload != proto::FileId{} && a.copy_word == 0
    };
}

Delivered HeldTransfer::misrouted(const proto::LinkOp&, const LinkOpCtx&, AnswerOnly) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);

    return Delivered{ILinkEncoder::Outcome::Dropped, false};
}

static_assert(LinkOpAnswerSink<HeldTransfer>);

}
