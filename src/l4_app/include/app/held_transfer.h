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

#include "app/bracket_close.h"
#include "app/carrier.h"
#include "app/cheat_apply.h"
#include "app/core_block_roles.h"
#include "app/core_tenancy.h"
#include "app/delivered.h"
#include "app/event.h"
#include "app/file_tx_level.h"
#include "app/interned_file.h"
#include "app/link_binder.h"
#include "app/link_encoder.h"
#include "app/link_op_ctx.h"
#include "app/link_op_dispatch.h"
#include "app/link_tx_reader.h"
#include "app/osd_wire.h"
#include "app/session_bindings.h"
#include "app/session_machine.h"
#include "app/session_tally.h"
#include "app/slot_bindings.h"
#include "proto/link/spi_frame.h"
#include "app/tx_digest_cell.h"
#include "app/types.h"
#include "app/window_job_service.h"
#include "infra/endian.h"
#include "cores/core.h"
#include "cores/load_progress.h"
#include "cores/off_link_window.h"
#include "cores/progress_ticker.h"
#include "cores/staging_core.h"
#include "cores/stream_load.h"
#include "cores/stream_load_bracket.h"
#include "cores/window_job.h"
#include "hal/spi_transport.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "proto/block_roles.h"
#include "proto/link/core_session.h"
#include "proto/link/download_session.h"
#include "proto/link/frame_route.h"
#include "proto/image_bracket.h"
#include "proto/link_op.h"
#include "proto/no_block_roles.h"
#include "proto/osd_surface.h"
#include "proto/link/spi_fio_queue.h"
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

        infra::Tally off_link_windows{};
        infra::Tally off_link_cuts{};
        infra::Tally off_link_rounds{};
        infra::Tally off_link_refusals{};
    };

    struct Wiring {
        hal::ISpiTransport& link;

        const proto::link::CoreSession& session;
        LinkBinder& binder;

        SessionMachine& machine;
        infra::ErrorSite advise_site;
        const SessionBindings& bindings;
        ICheatApply& cheats;
        OsdWire& osd;

        SlotBindings& slot_bindings;
        const CoreTenancy& tenancy;
        SessionTally& session_tally;
        infra::OptRef<WindowJobService> window_jobs{};

        infra::OptRef<proto::link::FrameRoute> frames{};
    };

    static constexpr std::size_t kCopyWordsMax = 8;

    explicit HeldTransfer(const Wiring& w) noexcept;

    HeldTransfer(const HeldTransfer&) = delete;
    HeldTransfer& operator=(const HeldTransfer&) = delete;
    ~HeldTransfer() override = default;

    using Result = Delivered;
    Result on(const proto::LinkOp::FileTx& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::StagePayload& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::PlainDownload& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::StageDiscPayload& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::StageReset& a, const LinkOpCtx& ctx) noexcept;
    Result on(const proto::LinkOp::SetWideIndex& a, const LinkOpCtx& ctx) noexcept;
    Result misrouted(const proto::LinkOp& op, const LinkOpCtx& ctx) noexcept;

    Result on(const proto::LinkOp::FileTx& a, const LinkOpCtx& ctx, AnswerOnly) noexcept;
    Result on(const proto::LinkOp::StagePayload& a, const LinkOpCtx& ctx, AnswerOnly) noexcept;
    Result misrouted(const proto::LinkOp& op, const LinkOpCtx& ctx, AnswerOnly) noexcept;

    [[nodiscard]] bool hold_wire(bool park_asked) noexcept;

    [[nodiscard]] bool wire_held() const noexcept {
        TASTY_SEAT_BODY(HeldTransfer);
        return copy_ds_.has_value() || dat_window_held() || off_link_holds_ || link_.posted();
    }

    [[nodiscard]] bool off_link_sole_hold() const noexcept {
        TASTY_SEAT_BODY(HeldTransfer);
        return off_link_holds_ && !copy_ds_.has_value() && !link_.posted();
    }

    void bind_downloads(Carrier route) noexcept {
        TASTY_SEAT_BODY(HeldTransfer);
        downloads_ = route;
    }

    [[nodiscard]] bool dat_window_held() const noexcept {
        TASTY_SEAT_BODY(HeldTransfer);
        return plain_ds_ && plain_ds_->dat_held();
    }

    [[nodiscard]] bool keeps_window(const proto::LinkOp::PlainDownload& a) const noexcept;

    void release_window() noexcept;

    void cut_all() noexcept;

    void publish_window_level(proto::ActKey act, bool open) noexcept;

    void publish_payload_echo(proto::ActKey act, bool encoded) noexcept;

    void publish_stream(
        proto::ActKey act,
        bool served,
        std::optional<std::uint32_t> held = std::nullopt
    ) noexcept;

    [[nodiscard]] Delivered::Edge after_file_tx(
        proto::FioIndexByte index,
        std::string_view path,
        ContentEdge edge,
        cores::Core& current
    );

    [[nodiscard]] bool save_pull_fenced(infra::OptRef<cores::Core> current) noexcept;

    [[nodiscard]] bool file_tx_held() const noexcept { return ftx_open_.has_value(); }

    [[nodiscard]] bool stage_held() const noexcept { return stage_open_.has_value(); }

    [[nodiscard]] bool fio_held() const noexcept {
        return ftx_open_.has_value()
            || stage_open_.has_value()
            || plain_ds_.has_value()
            || off_link_open_();
    }

    [[nodiscard]] bool copy_posted() const noexcept { return copy_ds_.has_value(); }

    [[nodiscard]] std::optional<proto::FioIndexByte> last_download() const noexcept {
        return last_download_;
    }

    [[nodiscard]] proto::FileSize last_tx_bytes() const noexcept { return last_tx_bytes_; }

    [[nodiscard]] const FileTxLevelCell& file_tx_level_cell() const noexcept {
        return ftx_level_cell_;
    }

    [[nodiscard]] cores::ILoadProgress& progress() noexcept { return *this; }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

private:
    void on_load_progress(cores::Progress progress) noexcept override;

    void before_close(bool remounts_saves) noexcept override;

    void announce_open_save(proto::IBlockRoles& roles) noexcept;

    [[nodiscard]] std::optional<Delivered::Edge> stream_file_tx_(
        const proto::LinkOp::FileTx& a,
        infra::OptRef<const InternedFile> file,
        cores::Core& current
    );

    [[nodiscard]] bool open_stream_bracket_(
        const proto::LinkOp::FileTx& a,
        infra::OptRef<const InternedFile> file,
        cores::Core& current
    );

    [[nodiscard]] std::optional<Delivered::Edge> close_stream_bracket_(
        const proto::LinkOp::FileTx& a,
        const InternedFile& file,
        cores::Core& current
    );

    void refuse_open_(bool window, proto::ActKey act) noexcept;

    void cut_file_tx_(infra::OptRef<cores::Core> current) noexcept;

    [[nodiscard]] infra::OptRef<cores::IStreamLoad> live_load_(
        cores::Core& core,
        cores::StreamLoadKind kind
    ) noexcept;

    [[nodiscard]] ILinkEncoder::Outcome stage_copy_(
        const proto::LinkOp::StagePayload& a,
        std::span<const std::uint8_t> bytes
    ) noexcept;
    void end_copy_(bool ok) noexcept;

    [[nodiscard]] ILinkEncoder::Outcome stage_length_(
        const proto::LinkOp::StagePayload& a,
        const InternedFile& file
    ) noexcept;

    void end_length_(bool ok) noexcept;

    [[nodiscard]] ILinkEncoder::Outcome stage_payload_(
        const proto::LinkOp::StagePayload& a,
        const LinkOpCtx& ctx
    ) noexcept;

    void echo_payload_act_(const proto::LinkOp::StagePayload& a, bool encoded) noexcept;

    [[nodiscard]] bool drain_payload_(
        std::span<const std::uint8_t> bytes,
        std::uint16_t chunk,
        const InternedFile& from
    ) noexcept;
    [[nodiscard]] ILinkEncoder::Outcome drop_stage_() noexcept;
    [[nodiscard]] bool stage_fits_round_(std::size_t bytes) const noexcept;

    [[nodiscard]] ILinkEncoder::Outcome drop_plain_() noexcept;
    void end_plain_() noexcept;
    void cut_stage_() noexcept;
    void arm_open_save_(
        infra::OptRef<const InternedFile> file,
        const ILinkTxReader& inbox
    ) noexcept;

    [[nodiscard]] bool off_link_open_() const noexcept {
        return off_link_.has_value() || off_link_stage_.has_value();
    }

    [[nodiscard]] Delivered off_link_file_tx_(const proto::LinkOp::FileTx& a, const LinkOpCtx& ctx);
    [[nodiscard]] Delivered off_link_open_window_(
        const proto::LinkOp::FileTx& a,
        infra::OptRef<const InternedFile> file,
        cores::Core& current
    );

    [[nodiscard]] Delivered off_link_refuse_(bool window, proto::ActKey act);
    [[nodiscard]] Delivered off_link_close_(
        const proto::LinkOp::FileTx& a,
        infra::OptRef<const InternedFile> file,
        cores::Core& current
    );
    [[nodiscard]] ILinkEncoder::Outcome off_link_stage_payload_(
        const proto::LinkOp::StagePayload& a,
        const LinkOpCtx& ctx
    ) noexcept;

    [[nodiscard]] bool off_link_takes_queue_(infra::OptRef<cores::Core> current) noexcept;

    void end_off_link_(infra::OptRef<cores::Core> current, bool cut) noexcept;

    void hold_off_link_() noexcept;

    [[nodiscard]] bool owns_post_() const noexcept {
        return link_.posted() && !session_.configure_posted();
    }

    hal::ISpiTransport& link_;
    const proto::link::CoreSession& session_;
    LinkBinder& binder_;
    SessionMachine& machine_;
    const infra::ErrorSite advise_site_;
    const SessionBindings& bindings_;
    ICheatApply& cheats_;
    OsdWire& osd_;
    SlotBindings& slot_bindings_;
    const CoreTenancy& tenancy_;
    SessionTally& session_tally_;
    infra::OptRef<WindowJobService> window_jobs_;
    infra::OptRef<proto::link::FrameRoute> frames_;
    Tally tally_{};

    proto::FileSize last_tx_bytes_{};
    std::optional<proto::FioIndexByte> last_download_{};

    struct OpenSave {
        proto::PathId path{};
        proto::FileSize size{};
    };

    std::optional<OpenSave> open_save_;
    FileTxLevel ftx_level_scratch_{};
    FileTxLevelCell ftx_level_cell_{};

    std::optional<proto::link::DownloadSession> copy_ds_{};
    std::array<hal::LinkWord, kCopyWordsMax> copy_tail_{};
    std::uint8_t copy_tail_n_ = 0;
    proto::ActKey copy_act_{};

    proto::ActKey length_act_{};

    cores::ProgressTicker stage_tick_{};

    std::optional<cores::StreamLoadBracket> ftx_open_{};

    std::optional<proto::ImageBracket> stage_open_{};

    std::optional<proto::link::DownloadSession> plain_ds_{};

    Carrier downloads_ = Carrier::Spi;
    std::optional<cores::OffLinkWindow> off_link_{};
    std::optional<proto::WideIoIndex> off_link_stage_{};
    bool off_link_holds_ = false;
};

static_assert(LinkOpTransferSink<HeldTransfer>);

}
