// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include "app/board_ops.h"
#include "app/bracket_close.h"
#include "app/cheat_apply.h"
#include "app/core_tenancy.h"
#include "app/event.h"
#include "app/file_tx_level.h"
#include "app/interned_file.h"
#include "app/link_binder.h"
#include "app/link_encoder.h"
#include "app/link_op_ctx.h"
#include "app/link_op_dispatch.h"
#include "app/link_tx_reader.h"
#include "app/osd_wire.h"
#include "app/rt_relax_scope.h"
#include "app/session_bindings.h"
#include "app/session_machine.h"
#include "app/session_tally.h"
#include "app/slot_bindings.h"
#include "app/tx_digest_cell.h"
#include "app/types.h"
#include "app/window_job_service.h"
#include "cores/core_support.h"
#include "cores/load_progress.h"
#include "cores/progress_ticker.h"
#include "cores/save_channel.h"
#include "cores/staging_core.h"
#include "cores/stream_load.h"
#include "cores/stream_load_bracket.h"
#include "cores/window_job.h"
#include "hal/spi_transport.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/download_session.h"
#include "proto/image_bracket.h"
#include "proto/link_op.h"
#include "proto/osd_surface.h"
#include "proto/spi_fio_queue.h"
#include "proto/types.h"

namespace mister::svc {
class IFile;
}

namespace mister::app {

class HeldTransfer final : private cores::ILoadProgress, private IBracketClose {
    TASTY_SEAT_RESIDENT(RT);

public:
    struct Tally {

        std::uint32_t downloads_closed = 0;

        std::uint32_t file_tx_piece_drops = 0;

        std::uint32_t file_tx_cuts = 0;

        std::uint32_t open_saves_mounted = 0;
        std::uint32_t open_save_refusals = 0;

        std::uint32_t save_pulls_fenced = 0;

        std::uint32_t copy_held_rounds = 0;

        std::uint32_t file_tx_holds = 0;
        std::uint32_t file_tx_held_rounds = 0;
        std::uint32_t file_tx_hold_abandons = 0;
        std::uint32_t stage_drops = 0;
        std::uint32_t stage_cuts = 0;

        std::uint32_t link_op_misrouted = 0;
    };

    struct Wiring {
        hal::ISpiTransport& link;
        LinkBinder& binder;

        SessionMachine& machine;
        EmitSite advise_site;
        const SessionBindings& bindings;
        ICheatApply& cheats;
        OsdWire& osd;
        BoardOps ops;

        SlotBindings& slot_bindings;
        const CoreTenancy& tenancy;
        SessionTally& session_tally;
        infra::OptRef<WindowJobService> window_jobs{};
    };

    static constexpr std::size_t kCopyWordsMax = 8;

    explicit HeldTransfer(const Wiring& w) noexcept
        : link_(w.link),
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

    HeldTransfer(const HeldTransfer&) = delete;
    HeldTransfer& operator=(const HeldTransfer&) = delete;
    ~HeldTransfer() override = default;

    using Result = ILinkEncoder::Outcome;
    Result on(const proto::LinkOp::FileTx& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::StagePayload& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::StageDiscPayload& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::StageReset& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::SetWideIndex& a, const LinkOpCtx& ctx) noexcept;
    Result misrouted(const proto::LinkOp& op, const LinkOpCtx& ctx) noexcept;

    [[nodiscard]] bool hold_wire(bool park_asked) noexcept;

    [[nodiscard]] bool wire_held() const noexcept {
        TASTY_SEAT_BODY(HeldTransfer);
        return copy_ds_.has_value() || link_.posted();
    }

    void cut_all() noexcept;

    void publish_ftx_level(std::uint32_t act, bool open, bool payload = false) noexcept;

    void publish_stream(
        std::uint32_t act,
        bool served,
        std::optional<std::uint32_t> held = std::nullopt
    ) noexcept;

    void after_file_tx(
        std::uint8_t slot,
        std::string_view path,
        ContentEdge edge,
        cores::Core& current
    );

    [[nodiscard]] bool save_pull_fenced(infra::OptRef<cores::Core> current) noexcept;

    [[nodiscard]] bool file_tx_held() const noexcept { return ftx_open_.has_value(); }

    [[nodiscard]] bool stage_held() const noexcept { return stage_open_.has_value(); }

    [[nodiscard]] bool fio_held() const noexcept {
        return ftx_open_.has_value() || stage_open_.has_value();
    }

    [[nodiscard]] bool copy_posted() const noexcept { return copy_ds_.has_value(); }

    [[nodiscard]] std::uint8_t last_download_index() const noexcept {
        return static_cast<std::uint8_t>(ftx_index_);
    }

    [[nodiscard]] std::uint64_t last_tx_bytes() const noexcept { return ftx_bytes_; }

    [[nodiscard]] std::uint16_t last_tx_index() const noexcept { return ftx_index_; }

    [[nodiscard]] const FileTxLevelCell& file_tx_level_cell() const noexcept {
        return ftx_level_cell_;
    }

    [[nodiscard]] cores::ILoadProgress& progress() noexcept { return *this; }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

private:
    void on_load_progress(std::uint16_t cur, std::uint16_t max) noexcept override;

    void before_close() noexcept override;

    void announce_open_save() noexcept;

    [[nodiscard]] bool stream_file_tx_(
        const proto::LinkOp::FileTx& a,
        const InternedFile* file,
        cores::Core& current
    );

    [[nodiscard]] Ex<void> perform_file_tx_(
        std::span<const std::uint8_t> bytes,
        std::string_view path,
        std::uint8_t slot,
        ContentEdge edge,
        cores::Core& current
    );

    void cut_file_tx_() noexcept;

    [[nodiscard]] ILinkEncoder::Outcome stage_copy_(
        const proto::LinkOp::StagePayload& a,
        std::span<const std::uint8_t> bytes
    ) noexcept;
    void end_copy_(bool ok) noexcept;

    [[nodiscard]] bool drain_payload_(
        std::span<const std::uint8_t> bytes,
        std::uint16_t chunk,
        std::uint64_t at
    ) noexcept;
    [[nodiscard]] ILinkEncoder::Outcome drop_stage_() noexcept;
    [[nodiscard]] bool stage_fits_round_(std::size_t bytes) const noexcept;
    void cut_stage_() noexcept;
    void arm_open_save_(const InternedFile* file, const ILinkTxReader& inbox) noexcept;

    hal::ISpiTransport& link_;
    LinkBinder& binder_;
    SessionMachine& machine_;
    const EmitSite advise_site_;
    const SessionBindings& bindings_;
    ICheatApply& cheats_;
    OsdWire& osd_;
    const BoardOps ops_;
    SlotBindings& slot_bindings_;
    const CoreTenancy& tenancy_;
    SessionTally& session_tally_;
    infra::OptRef<WindowJobService> window_jobs_;
    Tally tally_{};

    bool progress_opened_osd_ = false;

    std::uint64_t ftx_bytes_ = 0;
    std::uint16_t ftx_index_ = 0;

    struct OpenSave {
        proto::PathId path{};
        proto::FileSize size{};
    };

    std::optional<OpenSave> open_save_;
    FileTxLevel ftx_level_scratch_{};
    FileTxLevelCell ftx_level_cell_{};

    std::optional<proto::DownloadSession> copy_ds_{};
    std::array<std::uint16_t, kCopyWordsMax> copy_tail_{};
    std::uint8_t copy_tail_n_ = 0;
    std::uint16_t copy_act_ = 0;

    cores::ProgressTicker stage_tick_{};

    std::optional<cores::StreamLoadBracket> ftx_open_{};

    std::optional<proto::ImageBracket> stage_open_{};
};

inline ILinkEncoder::Outcome HeldTransfer::on(
    const proto::LinkOp::StagePayload& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    using Phase = proto::LinkOp::StagePhase;
    const bool opens = a.phase == Phase::Whole || a.phase == Phase::Open;
    const bool closes = a.phase == Phase::Whole || a.phase == Phase::Close;
    cores::IStagingCore* cd = ctx.core.has_value() ? ctx.core->staging_core() : nullptr;

    std::optional<RtRelaxScope> relaxed;
    if (a.copy_word == 0 || a.phase != Phase::Whole)
        relaxed.emplace(ops_);

    if (!ctx.core.has_value() || (cd == nullptr && proto::sets_region(a))) {
        return drop_stage_();
    }
    const InternedFile* file = nullptr;
    std::span<const std::uint8_t> bytes{};
    if (a.payload != proto::FileId{}) {
        file = ctx.inbox.file(a.payload);
        if (file == nullptr)
            return drop_stage_();
        bytes = file->bytes;
    }

    const bool load_phase = a.phase == Phase::Whole && ctx.starting;
    if (!load_phase && !stage_fits_round_(bytes.size()))
        return drop_stage_();
    if (a.copy_word != 0) {
        return a.phase == Phase::Whole ? stage_copy_(a, bytes) : drop_stage_();
    }

    if (a.phase == Phase::Close && file == nullptr) {
        if (!stage_open_)
            return drop_stage_();
        cut_stage_();
        return ILinkEncoder::Outcome::Encoded;
    }
    if (opens) {
        cut_stage_();
        cut_file_tx_();
        proto::SessionParams params{};
        if (file != nullptr) {
            params.ext = file->ext.view();
            params.aux = proto::AuxValue{file->aux};
        }
        proto::IImageSink& sink = cd != nullptr ? cd->image_sink() : ctx.core->image_sink();
        auto ds = proto::ImageBracket::open(sink, a.dest, params);
        if (!ds)
            return ILinkEncoder::Outcome::Dropped;
        stage_open_.emplace(std::move(*ds));
        stage_tick_.bind(a.progress ? this : nullptr);
        stage_tick_.begin(file != nullptr ? file->size_bytes : 0);
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
    if (proto::sets_region(a) && cd != nullptr)
        cd->set_region(a.region);
    return ILinkEncoder::Outcome::Encoded;
}

inline ILinkEncoder::Outcome HeldTransfer::on(
    const proto::LinkOp::StageDiscPayload& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    cores::IStagingCore* cd = ctx.core.has_value() ? ctx.core->staging_core() : nullptr;
    if (cd == nullptr)
        return ILinkEncoder::Outcome::Dropped;
    RtRelaxScope scope(ops_);
    if (auto r = cd->stage_disc_payload(a); !r)
        return ILinkEncoder::Outcome::Dropped;
    return ILinkEncoder::Outcome::Encoded;
}

inline ILinkEncoder::Outcome HeldTransfer::on(
    const proto::LinkOp::StageReset&,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    cores::IStagingCore* cd = ctx.core.has_value() ? ctx.core->staging_core() : nullptr;
    if (cd == nullptr)
        return ILinkEncoder::Outcome::Dropped;
    if (auto r = cd->deferred_reset(); !r)
        return ILinkEncoder::Outcome::Dropped;
    return ILinkEncoder::Outcome::Encoded;
}

inline ILinkEncoder::Outcome HeldTransfer::on(
    const proto::LinkOp::SetWideIndex& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    cores::IStagingCore* cd = ctx.core.has_value() ? ctx.core->staging_core() : nullptr;

    if (cd == nullptr) {
        return ILinkEncoder::Outcome::Dropped;
    }
    (void)binder_.fio_queue().flush();
    if (auto r = proto::DownloadSession::set_index(link_, a.dest); !r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

inline ILinkEncoder::Outcome HeldTransfer::misrouted(
    const proto::LinkOp&,
    const LinkOpCtx&
) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    ++tally_.link_op_misrouted;
    return ILinkEncoder::Outcome::Dropped;
}

inline void HeldTransfer::arm_open_save_(
    const InternedFile* file,
    const ILinkTxReader& inbox
) noexcept {
    open_save_.reset();
    if (file == nullptr || file->save == proto::FileId{})
        return;
    const InternedFile* const save = inbox.file(file->save);
    if (save == nullptr) {
        ++tally_.open_save_refusals;
        return;
    }
    open_save_ = OpenSave{
        .path = proto::PathId{file->save.v},
        .size = proto::FileSize{save->size_bytes}
    };
}

inline void HeldTransfer::announce_open_save() noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    if (!open_save_)
        return;
    const OpenSave s = *open_save_;
    open_save_.reset();
    if (!slot_bindings_.apply_bind_slot(
            proto::SlotIndex{0},
            proto::LinkOp::SlotBind::Mount,
            s.size,
            s.path
        )) {
        ++tally_.open_save_refusals;
        return;
    }
    ++tally_.open_saves_mounted;
}

inline void HeldTransfer::before_close() noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    announce_open_save();
    if (!tenancy_.core())
        return;
    if (cores::ISaveChannel* ch = tenancy_.core()->save_channel())
        slot_bindings_.mount_save_channel(*ch, tenancy_.poisoned_slots());
}

inline bool HeldTransfer::stream_file_tx_(
    const proto::LinkOp::FileTx& a,
    const InternedFile* file,
    cores::Core& current
) {
    using Phase = proto::LinkOp::FileTxPhase;

    const bool window = a.phase == Phase::WindowOpen || a.phase == Phase::WindowClose;
    const bool opens = a.phase == Phase::Whole
        || a.phase == Phase::Open
        || a.phase == Phase::WindowOpen;
    const bool closes = a.phase == Phase::Whole
        || a.phase == Phase::Close
        || a.phase == Phase::WindowClose;
    if (opens) {
        if (ftx_open_)
            cut_file_tx_();
        if (file == nullptr || (!window && file->bytes.empty())) {
            if (a.phase != Phase::Whole)
                ++tally_.file_tx_piece_drops;
            if (window)
                publish_ftx_level(a.act, false);
            return false;
        }
        const std::string_view path = file->path.view();
        const std::size_t dot = path.rfind('.');
        current.set_pending_file_ext(
            dot == std::string_view::npos ? std::string_view{} : path.substr(dot)
        );
        current.set_pending_file_path(path);

        if (!window) {
            binder_.file_progress().arm(
                *this,
                a.phase == Phase::Whole ? file->bytes.size() : a.total
            );
        }
        cores::IStreamLoad& load = window ? *current.window_load() : *current.stream_load();
        auto b = cores::StreamLoadBracket::open(
            load,
            current.image_sink(),
            proto::IoIndex{a.wire_index},
            window ? a.total : 0
        );
        if (!b) {
            if (window) {
                publish_ftx_level(a.act, false);
            } else {
                binder_.file_progress().disarm();
            }
            return false;
        }
        ftx_open_.emplace(std::move(*b));
        if (window) {

            cores::IWindowSave* role = current.window_save();
            if (role != nullptr
                && window_jobs_
                && window_jobs_->hold_open(*role, proto::IoIndex{a.wire_index}, a.act)) {
                return true;
            }
            publish_ftx_level(a.act, true);
            return true;
        }
    } else if (!ftx_open_) {
        ++tally_.file_tx_piece_drops;
        return false;
    }

    if ((a.phase == Phase::Close || a.phase == Phase::WindowClose) && file == nullptr) {
        cut_file_tx_();
        return true;
    }
    if (!window) {

        if (file == nullptr) {
            ++tally_.file_tx_piece_drops;
            cut_file_tx_();
            return false;
        }
        const auto taken = file->bytes.empty() ? Ex<std::size_t>{0u}
                                               : ftx_open_->write_taken(file->bytes);
        if (!taken) {
            ++tally_.file_tx_piece_drops;
            cut_file_tx_();
            return false;
        }

        if (link_.posted()) {
            ++tally_.file_tx_holds;
            open_save_.reset();
            publish_stream(a.act, true, static_cast<std::uint32_t>(*taken));
            return true;
        }
    }
    if (!closes)
        return true;

    before_close();
    const std::uint64_t total = a.phase == Phase::Whole ? file->bytes.size() : a.total;
    const bool ok = ftx_open_->close(total, file->crc).has_value();
    ftx_open_.reset();
    if (!window)
        binder_.file_progress().disarm();
    if (!ok)
        return false;

    if (!window || current.stream_load() != nullptr)
        after_file_tx(a.wire_index, file->path.view(), file->edge, current);
    return true;
}

inline void HeldTransfer::after_file_tx(
    std::uint8_t slot,
    std::string_view path,
    ContentEdge edge,
    cores::Core& current
) {
    TASTY_SEAT_BODY(HeldTransfer);
    ++tally_.downloads_closed;
    ftx_index_ = slot;
    ftx_bytes_ = current.last_tx_bytes();

    if (bindings_.declares_cheats() && edge != ContentEdge::Silent) {
        cheats_.content_loaded(
            &current,
            TxDigest::Kind::Load,
            edge == ContentEdge::ChainEnd ? std::string_view{} : path,
            current.last_tx_crc(),
            false
        );
    }
    machine_.advise(Event::SdActivity{.slot = slot}, advise_site_);
    if (cores::ISaveChannel* ch = current.save_channel()) {
        slot_bindings_.mount_save_channel(*ch, tenancy_.poisoned_slots());
    }
}

inline void HeldTransfer::publish_ftx_level(std::uint32_t act, bool open, bool payload) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    ftx_level_scratch_.act = act;
    ftx_level_scratch_.open = open;
    ftx_level_scratch_.payload = payload;
    ftx_level_cell_.publish(ftx_level_scratch_);
}

inline void HeldTransfer::publish_stream(
    std::uint32_t act,
    bool served,
    std::optional<std::uint32_t> held
) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    if (act == 0)
        return;
    ftx_level_scratch_.stream_act = act;
    ftx_level_scratch_.stream_served = served;
    if (held) {
        ftx_level_scratch_.held_act = act;
        ftx_level_scratch_.held_bytes = *held;
    }
    ftx_level_cell_.publish(ftx_level_scratch_);
}

inline bool HeldTransfer::save_pull_fenced(infra::OptRef<cores::Core> current) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    if ((!ftx_open_ && !stage_open_) || current->save_upload() == nullptr)
        return false;
    ++tally_.save_pulls_fenced;
    return true;
}

inline void HeldTransfer::cut_file_tx_() noexcept {
    if (!ftx_open_)
        return;
    ftx_open_.reset();
    ++tally_.file_tx_cuts;
    binder_.file_progress().disarm();
}

inline ILinkEncoder::Outcome HeldTransfer::stage_copy_(
    const proto::LinkOp::StagePayload& a,
    std::span<const std::uint8_t> bytes
) noexcept {
    cut_stage_();
    const std::size_t n = bytes.size() / 2u;
    if (copy_ds_ || proto::sets_region(a) || a.copy_word > n || n > kCopyWordsMax) {
        return ILinkEncoder::Outcome::Dropped;
    }
    std::array<std::uint16_t, kCopyWordsMax> w{};
    for (std::size_t i = 0; i < n; ++i)
        w[i] = static_cast<std::uint16_t>(bytes[2u * i] | (bytes[2u * i + 1u] << 8));
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
    copy_act_ = a.act;
    copy_ds_.emplace(std::move(*ds));
    return ILinkEncoder::Outcome::Encoded;
}

inline bool HeldTransfer::hold_wire(bool park_asked) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    if (!wire_held())
        return false;
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

inline void HeldTransfer::end_copy_(bool ok) noexcept {
    copy_ds_.reset();
    if (copy_act_ != 0)
        publish_ftx_level(copy_act_, ok, true);
    if (!ok)
        ++session_tally_.link_op_drops;
    copy_act_ = 0;
    copy_tail_n_ = 0;
}

inline void HeldTransfer::cut_all() noexcept {
    TASTY_SEAT_BODY(HeldTransfer);

    if (copy_ds_) {
        copy_ds_->abandon();
        end_copy_(false);
    }
    if (link_.posted()) {
        link_.abandon_post();
        ++tally_.file_tx_hold_abandons;
    }

    cut_file_tx_();
    cut_stage_();
    if (window_jobs_)
        window_jobs_->forget_held();
}

inline bool HeldTransfer::drain_payload_(
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

inline ILinkEncoder::Outcome HeldTransfer::drop_stage_() noexcept {
    ++tally_.stage_drops;
    cut_stage_();
    return ILinkEncoder::Outcome::Dropped;
}

inline bool HeldTransfer::stage_fits_round_(std::size_t bytes) const noexcept {
    const std::size_t words = link_.width() == hal::Width::Word ? (bytes + 1u) / 2u : bytes;
    return words <= proto::tightest_load_budget_words();
}

inline void HeldTransfer::cut_stage_() noexcept {
    if (!stage_open_)
        return;
    stage_open_.reset();
    ++tally_.stage_cuts;
    stage_tick_.finish();
}

inline void HeldTransfer::on_load_progress(std::uint16_t cur, std::uint16_t max) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);

    machine_.advise(Event::ProgressUpdate{.cur = cur, .max = max}, advise_site_);

    if (link_.posted())
        return;

    const bool done = max == 0;

    if (!done && !progress_opened_osd_ && !osd_.lit()) {
        progress_opened_osd_ = true;
        (void)osd_.set_show(proto::LinkOp::OsdShow::Overlay);
    } else if (done && progress_opened_osd_) {
        progress_opened_osd_ = false;
        (void)osd_.set_show(proto::LinkOp::OsdShow::Off);
    }

    for (unsigned i = 0; i < proto::OsdSurface::kMaxRows && osd_.flush_one(); ++i) {
    }
}

static_assert(LinkOpTransferSink<HeldTransfer>);

}
