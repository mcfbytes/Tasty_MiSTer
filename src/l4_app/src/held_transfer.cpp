// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/held_transfer.h"

namespace mister::app {

Delivered HeldTransfer::on(const proto::LinkOp::FileTx& a, const LinkOpCtx& ctx) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    using Phase = proto::LinkOp::FileTxPhase;

    const bool spends = a.phase != Phase::Whole;

    const bool window = a.phase == Phase::WindowOpen || a.phase == Phase::WindowClose;
    if (!ctx.core.has_value()
        || machine_.state() == SessionState::Failed
        || (!(window ? ctx.core->window_load() : ctx.core->stream_load())
            && a.phase != Phase::Whole)) {
        if (a.phase != Phase::Whole)
            ++tally_.file_tx_piece_drops;

        if (a.phase == Phase::WindowOpen)
            publish_ftx_level(a.act, false);
        if (!window)
            publish_stream(a.act, false);
        return Delivered{ILinkEncoder::Outcome::Dropped, spends};
    }
    const InternedFile* const file = ctx.inbox.file(a.file);
    RtRelaxScope scope(ops_);
    cut_stage_();
    if (a.phase == Phase::Whole || a.phase == Phase::Close)
        arm_open_save_(file, ctx.inbox);

    bool ok = false;
    if (window || ctx.core->stream_load()) {
        ok = stream_file_tx_(a, file, *ctx.core);
    } else if (file != nullptr && !file->bytes.empty()) {
        ok = perform_file_tx_(
                 file->bytes,
                 file->path.view(),
                 proto::IoIndex{a.wire_index},
                 file->edge,
                 *ctx.core
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
    cores::Core& current
) {
    const std::size_t dot = path.rfind('.');
    current.set_pending_file_ext(
        dot == std::string_view::npos ? std::string_view{} : path.substr(dot)
    );
    current.set_pending_file_path(path);

    ProgressImageSink& meter = binder_.file_progress();
    meter.arm(*this, bytes.size(), this);
    const auto r = current.file_tx(index, bytes);
    meter.disarm();
    if (!r)
        return r;
    after_file_tx(index, path, edge, current);
    return {};
}

Delivered HeldTransfer::on(const proto::LinkOp::FileTx& a, const LinkOpCtx&, AnswerOnly) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    using Phase = proto::LinkOp::FileTxPhase;

    if (a.phase == Phase::WindowOpen) {
        publish_ftx_level(a.act, false);
    } else if (a.phase != Phase::WindowClose) {
        publish_stream(a.act, false);
    }

    return Delivered{ILinkEncoder::Outcome::Dropped, a.phase != Phase::Whole};
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
