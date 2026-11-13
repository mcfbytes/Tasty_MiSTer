// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/held_transfer.h"

#include "app/delivered.h"
#include "infra/path_parts.h"

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
      ops_(w.ops),
      slot_bindings_(w.slot_bindings),
      tenancy_(w.tenancy),
      session_tally_(w.session_tally),
      window_jobs_(w.window_jobs) {}

Delivered HeldTransfer::on(const proto::LinkOp::FileTx& a, const LinkOpCtx& ctx) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    using Phase = proto::LinkOp::FileTxPhase;

    const bool spends = a.phase != Phase::Whole && a.phase != Phase::Empty;

    const bool window = a.phase == Phase::WindowOpen || a.phase == Phase::WindowClose;
    if (!ctx.core.has_value()
        || machine_.state() == SessionState::Failed
        || (!(window ? ctx.core->window_load() : ctx.core->stream_load())
            && a.phase != Phase::Whole)) {
        if (a.phase != Phase::Whole)
            ++tally_.file_tx_piece_drops;

        if (a.phase == Phase::WindowOpen)
            publish_window_level(a.act, false);
        if (!window)
            publish_stream(a.act, false);
        return Delivered{ILinkEncoder::Outcome::Dropped, spends};
    }
    const infra::OptRef<const InternedFile> file = ctx.tx.file(a.file);
    RtRelaxScope scope(ops_);
    cut_stage_();
    if (a.phase == Phase::Whole || a.phase == Phase::Close || a.phase == Phase::Empty)
        arm_open_save_(file, ctx.tx);

    bool ok = false;
    if (window || ctx.core->stream_load()) {
        ok = stream_file_tx_(a, file, *ctx.core);
    } else if (file && !file->bytes.empty()) {
        ok = perform_file_tx_(
                 file->bytes,
                 file->path.view(),
                 proto::IoIndex{a.wire_index},
                 file->edge,
                 *ctx.core,
                 file->remounts_saves
        )
                 .has_value();
    }
    open_save_.reset();
    if (!window && !(ok && link_.posted()))
        publish_stream(a.act, ok);
    if (!ok)
        return Delivered{ILinkEncoder::Outcome::Dropped, spends};
    return Delivered{ILinkEncoder::Outcome::Encoded, spends};
}

Ex<void> HeldTransfer::perform_file_tx_(
    std::span<const std::uint8_t> bytes,
    std::string_view path,
    proto::IoIndex index,
    ContentEdge edge,
    cores::Core& current,
    bool remounts_saves
) {
    current.set_pending_file_ext(infra::suffix_from_last_dot(path));
    current.set_pending_file_path(path);

    ProgressImageSink& meter = binder_.file_progress();
    meter.arm(*this, bytes.size(), infra::OptRef<IBracketClose>{*this}, remounts_saves);
    const auto r = current.file_tx(index, bytes);
    meter.disarm();
    if (!r)
        return r;
    after_file_tx(index, path, edge, current);
    return {};
}

Delivered HeldTransfer::on(const proto::LinkOp::StagePayload& a, const LinkOpCtx& ctx) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    const ILinkEncoder::Outcome r = stage_payload_(a, ctx);
    echo_payload_act_(a, r != ILinkEncoder::Outcome::Dropped);
    return Delivered{r, a.payload != proto::FileId{} && a.copy_word == 0};
}

void HeldTransfer::echo_payload_act_(const proto::LinkOp::StagePayload& a, bool encoded) noexcept {
    if (a.act && !copy_posted())
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

    std::optional<RtRelaxScope> relaxed;
    if (a.copy_word == 0 || a.phase != Phase::Whole)
        relaxed.emplace(ops_);

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
    if (!bytes.empty() && !drain_payload_(bytes, a.chunk, file->offset)) {
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
    RtRelaxScope scope(ops_);
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
    proto::SpiFrame frame{link_};
    if (auto r = proto::DownloadSession::set_index(frame, a.dest); !r) {
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

bool HeldTransfer::stream_file_tx_(
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
            return false;
        if (window)
            return true;
    } else if (!ftx_open_) {
        ++tally_.file_tx_piece_drops;
        return false;
    }

    if ((a.phase == Phase::Close || a.phase == Phase::WindowClose) && !file) {
        cut_file_tx_(here);
        return true;
    }
    if (!window && a.phase != Phase::Empty) {

        if (!file) {
            ++tally_.file_tx_piece_drops;
            cut_file_tx_(here);
            return false;
        }
        const auto taken = ftx_open_->write_taken(file->bytes);
        if (!taken) {
            ++tally_.file_tx_piece_drops;
            cut_file_tx_(here);
            return false;
        }

        if (link_.posted()) {
            ++tally_.file_tx_holds;
            open_save_.reset();
            publish_stream(a.act, true, static_cast<std::uint32_t>(taken->size()));
            return true;
        }
    }
    if (!closes)
        return true;
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
    const std::string_view path = file->path.view();
    current.set_pending_file_ext(infra::suffix_from_last_dot(path));
    current.set_pending_file_path(path);

    if (!window) {
        binder_.file_progress().arm(*this, a.phase == Phase::Whole ? file->bytes.size() : a.total);
    }
    const auto load = window ? current.window_load() : current.stream_load();
    if (!load) {
        refuse_open_(window, a.act);
        return false;
    }
    auto b = cores::StreamLoadBracket::open(
        *load,
        current.image_sink(),
        proto::IoIndex{a.wire_index},
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
    if (role
        && window_jobs_
        && window_jobs_->hold_open(*role, proto::IoIndex{a.wire_index}, a.act)) {
        return true;
    }
    publish_window_level(a.act, true);
    return true;
}

bool HeldTransfer::close_stream_bracket_(
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
        return false;

    if (!window || current.stream_load())
        after_file_tx(proto::IoIndex{a.wire_index}, file.path.view(), file.edge, current);
    return true;
}

void HeldTransfer::refuse_open_(bool window, proto::ActKey act) noexcept {
    if (window) {
        publish_window_level(act, false);
    } else {
        binder_.file_progress().disarm();
    }
}

void HeldTransfer::after_file_tx(
    proto::IoIndex index,
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
}

void HeldTransfer::publish_window_level(proto::ActKey act, bool open) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    ftx_level_scratch_.act = act;
    ftx_level_scratch_.open = open;
    ftx_level_scratch_.payload = false;
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
    if ((!ftx_open_ && !stage_open_) || !current->save_upload())
        return false;
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

ILinkEncoder::Outcome HeldTransfer::stage_copy_(
    const proto::LinkOp::StagePayload& a,
    std::span<const std::uint8_t> bytes
) noexcept {
    cut_stage_();
    const std::size_t n = bytes.size() / sizeof(std::uint16_t);
    if (copy_ds_ || proto::sets_region(a) || a.copy_word > n || n > kCopyWordsMax) {
        return ILinkEncoder::Outcome::Dropped;
    }
    std::array<std::uint16_t, kCopyWordsMax> w{};

    infra::load_le_run(bytes.first(n * sizeof(std::uint16_t)), std::span(w).first(n));
    (void)binder_.fio_queue().flush();
    auto ds = proto::DownloadSession::begin(link_, a.dest);
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

bool HeldTransfer::hold_wire(bool park_asked) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    if (!wire_held())
        return false;

    if (session_.configure_posted())
        return true;
    if (!copy_ds_) {

        if (park_asked) {
            link_.abandon_post();
            ++tally_.file_tx_hold_abandons;
            return false;
        }
        const auto done = link_.posted_done();
        if (done && !*done) {
            ++tally_.file_tx_held_rounds;
            return true;
        }
        return false;
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

    cut_file_tx_(tenancy_.core());
    cut_stage_();
    if (window_jobs_)
        window_jobs_->forget_held();
}

bool HeldTransfer::drain_payload_(
    std::span<const std::uint8_t> bytes,
    std::uint16_t chunk_bytes,
    std::uint64_t at
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
        stage_tick_.advance(at + off);
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
    return words <= proto::tightest_load_budget_words();
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
