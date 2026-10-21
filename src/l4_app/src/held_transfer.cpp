// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/held_transfer.h"

namespace mister::app {

ILinkEncoder::Outcome HeldTransfer::on(
    const proto::LinkOp::FileTx& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(HeldTransfer);
    using Phase = proto::LinkOp::FileTxPhase;

    const bool window = a.phase == Phase::WindowOpen || a.phase == Phase::WindowClose;
    if (!ctx.core.has_value()
        || machine_.state() == SessionState::Failed
        || ((window ? ctx.core->window_load() : ctx.core->stream_load()) == nullptr
            && a.phase != Phase::Whole)) {
        if (a.phase != Phase::Whole)
            ++tally_.file_tx_piece_drops;

        if (a.phase == Phase::WindowOpen)
            publish_ftx_level(a.act, false);
        if (!window)
            publish_stream(a.act, false);
        return ILinkEncoder::Outcome::Dropped;
    }
    const InternedFile* const file = ctx.inbox.file(a.file);
    RtRelaxScope scope(ops_);
    cut_stage_();
    if (a.phase == Phase::Whole || a.phase == Phase::Close)
        arm_open_save_(file, ctx.inbox);

    bool ok = false;
    if (window || ctx.core->stream_load() != nullptr) {
        ok = stream_file_tx_(a, file, *ctx.core);
    } else if (file != nullptr && !file->bytes.empty()) {
        ok = perform_file_tx_(file->bytes, file->path.view(), a.wire_index, file->edge, *ctx.core)
                 .has_value();
    }
    open_save_.reset();
    if (!window && !(ok && link_.posted()))
        publish_stream(a.act, ok);
    return ok ? ILinkEncoder::Outcome::Encoded : ILinkEncoder::Outcome::Dropped;
}

Ex<void> HeldTransfer::perform_file_tx_(
    std::span<const std::uint8_t> bytes,
    std::string_view path,
    std::uint8_t slot,
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
    const auto r = current.file_tx(proto::IoIndex{slot}, bytes);
    meter.disarm();
    if (!r)
        return r;
    after_file_tx(slot, path, edge, current);
    return {};
}

}
