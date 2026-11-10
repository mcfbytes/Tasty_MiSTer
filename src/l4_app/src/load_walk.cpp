// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/load_walk.h"

#include <algorithm>
#include <span>
#include <vector>

#include "app/load_window.h"
#include "infra/endian.h"
#include "cores/transfer_row.h"
#include "proto/link_op.h"

namespace mister::app {

Ex<LoadWalk> LoadWalk::start(
    std::unique_ptr<cores::ILoader> loader,
    const cores::LoadPlan& plan,
    std::span<const cores::CoreWindowDecl> windows
) {
    if (loader == nullptr
        || plan.count > plan.rows.size()
        || (plan.count == 0 && !plan.frame.hold_reset)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    for (std::size_t i = 0; i < plan.count; ++i) {
        if (plan.rows[i].dest == cores::RowDest::Fio)
            return std::unexpected(
                Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(i)}
            );
    }
    return LoadWalk(std::move(loader), plan, windows);
}

bool LoadWalk::wants_pass(
    const LinkTxChannel<SeatTag::Main>& inbox,
    const FileTxLevelCell& level
) const noexcept {
    TASTY_SEAT_BODY(LoadWalk);
    switch (pc_) {
        case Pc::Over:
            return false;
        case Pc::Rung:
            return ladder_ && ladder_->wants_pass(inbox, level);
        case Pc::Echo:
            return echo_wait_.ready(level);
        case Pc::SaveWait:
            return false;
        case Pc::Pieces:
            return inbox.ring().size() < cores::PayloadPieces::kAhead;
        case Pc::Hold:
        case Pc::Row:
        case Pc::Notify:
        case Pc::Save:
        case Pc::Release:
            return inbox.ring().size() < proto::kLinkTxCapacity;
    }

    return false;
}

LoadWalk::Pass LoadWalk::step(Host& h) {
    TASTY_SEAT_BODY(LoadWalk);
    switch (pc_) {
        case Pc::Hold:
            return hold_(h);
        case Pc::Row:
            return row_step_(h);
        case Pc::Rung:
            return rung_(h);
        case Pc::Pieces:
            return pieces_step_(h);
        case Pc::Echo:
            return echo_(h);
        case Pc::Notify:
            return notify_(h, plan_.rows[row_].notify);
        case Pc::Save:
            return save_(h);
        case Pc::SaveWait:
            return save_wait_(h);
        case Pc::Release:
            return release_(h);
        case Pc::Over:
            break;
    }
    return failed_ ? Pass::Failed : Pass::Done;
}

bool LoadWalk::status_bit0_(Host& h, bool asserted) {
    return h.rung.inbox.push(
        proto::LinkOp::WriteStatus{
            .start = proto::kResetBit,
            .width = 1,
            .value = asserted ? 1u : 0u
        }
    );
}

LoadWalk::Pass LoadWalk::hold_(Host& h) {
    if (plan_.frame.hold_reset && !status_bit0_(h, true))
        return Pass::Waiting;
    pc_ = Pc::Row;
    return Pass::Stepped;
}

void LoadWalk::next_row_() noexcept {
    ++row_;
    pc_ = Pc::Row;
}

LoadWalk::Pass LoadWalk::row_step_(Host& h) {
    if (row_ >= plan_.count) {

        if (plan_.frame.refused) {
            pc_ = Pc::Over;
            failed_ = true;
            return Pass::Failed;
        }
        pc_ = Pc::Save;
        return Pass::Stepped;
    }
    const cores::TransferRow& r = plan_.rows[row_];
    switch (r.dest) {
        case cores::RowDest::Window:
            return window_(h, r);
        case cores::RowDest::Payload:
            return payload_(h, r);
        case cores::RowDest::Notify:
            return notify_(h, r.notify);
        case cores::RowDest::Fio:
            break;
    }
    return fail_(h);
}

LoadWalk::Pass LoadWalk::window_(Host& h, const cores::TransferRow& r) {

    std::unique_ptr<svc::IFile> file;
    if (!r.source.empty() || !r.file_len.empty()) {
        auto f = h.vfs.open(r.source, svc::OpenMode::ReadWhole);
        if (!f)
            return skip_(r);
        file = std::move(*f);
    }
    const auto fabric = cores::row_address(r, windows_, h.aperture.phys);
    if (!fabric)
        return fail_(h);
    const std::uint64_t extent = r.extent_bytes(r.file_len).length_of(proto::BlockCount{1});
    auto w = LoadWindow<seat_owner::Main>::open(h.map, h.aperture, hal::to_phys(*fabric), extent);
    if (!w)
        return fail_(h);
    std::optional<LoadWindow<seat_owner::Main>> mirror{};
    if (r.mirror != 0) {
        auto m = LoadWindow<seat_owner::Main>::open(
            h.map,
            h.aperture,
            os::PhysAddr{r.mirror},
            extent
        );
        if (!m)
            return fail_(h);
        mirror.emplace(std::move(*m));
    }
    cores::TransferRow quiet = r;
    quiet.notify = {};

    const proto::ActKey act = r.bracket == cores::RowBracket::AroundWithLength
        ? h.owner.next_generation()
        : proto::ActKey{};
    auto l = LoadLadder::start(
        LoadLadder::Job{
            .loader = std::move(loader_),
            .file = std::move(file),
            .total = r.file_len.length_of(proto::BlockCount{1}),
            .wire_index = r.index,
            .act = act,
            .row = quiet,
            .mirror = std::move(mirror)
        },
        std::move(*w),
        r.source
    );
    if (!l)
        return fail_(h);
    ladder_.emplace(std::move(*l));
    pc_ = Pc::Rung;
    return Pass::Stepped;
}

LoadWalk::Pass LoadWalk::rung_(Host& h) {
    const LoadLadder::Pass pass = ladder_->step(h.rung);
    if (pass == LoadLadder::Pass::Stepped || pass == LoadLadder::Pass::Waiting) {
        return pass == LoadLadder::Pass::Stepped ? Pass::Stepped : Pass::Waiting;
    }
    loader_ = std::move(*ladder_).take_loader();
    ladder_.reset();
    if (pass == LoadLadder::Pass::Failed)
        return fail_(h);
    landed_ = true;
    const cores::TransferRow& r = plan_.rows[row_];
    if (!r.notify.armed) {
        next_row_();
        return Pass::Stepped;
    }
    pc_ = Pc::Notify;
    return notify_(h, r.notify);
}

LoadWalk::Pass LoadWalk::payload_(Host& h, const cores::TransferRow& r) {
    auto file = h.vfs.open(r.source, svc::OpenMode::ReadWhole);
    if (!file)
        return skip_(r);
    std::vector<std::uint8_t> raw(r.file_len.length_of(proto::BlockCount{1}));
    std::size_t got = 0;
    while (got < raw.size()) {
        auto n = (*file)->read_at(
            r.file_offset + got,
            std::as_writable_bytes(std::span(raw).subspan(got))
        );
        if (!n || *n == 0)
            break;
        got += *n;
    }
    raw.resize(
        std::max<std::size_t>(got, r.extent.length_of(proto::BlockCount{1})),
        std::uint8_t{0}
    );
    cores::WireChunk window = kStockFileTxChunk;
    if (r.byte_words) {

        std::vector<std::uint8_t> wide(2u * raw.size(), std::uint8_t{0});
        for (std::size_t i = 0; i < raw.size(); ++i)
            wide[2u * i] = raw[i];
        raw = std::move(wide);
        window = window.per_byte_word();
    }
    const std::uint64_t bytes = raw.size();
    auto p = cores::PayloadPieces::of_bytes(
        std::move(raw),
        {},
        {.dest = r.index, .chunk = window, .act = echo_wait_.mint(h.owner)}
    );
    if (!p)
        return fail_(h);
    pieces_.emplace(std::move(*p));
    echo_bytes_ = proto::FileSize{bytes};
    pc_ = Pc::Pieces;
    return pieces_step_(h);
}

LoadWalk::Pass LoadWalk::pieces_step_(Host& h) {
    const proto::ActKey16 act = pieces_->act();
    auto pass = pieces_->step(h.owner);
    if (pass && *pass == cores::PayloadPieces::Pass::Waiting)
        return Pass::Waiting;
    pieces_.reset();
    if (!pass)
        return fail_(h);
    echo_wait_.arm(act, h.now_ns, echo_bytes_, kEcho);
    pc_ = Pc::Echo;
    return Pass::Stepped;
}

LoadWalk::Pass LoadWalk::notify_(Host& h, const cores::PostNotify& n) {
    std::vector<std::uint8_t> words(n.words.size() * sizeof(std::uint16_t));
    infra::store_le_run(std::span<const std::uint16_t>(n.words), std::span(words));
    return send_(h, words, n);
}

LoadWalk::Pass LoadWalk::send_(
    Host& h,
    std::span<const std::uint8_t> bytes,
    const cores::PostNotify& notify
) {
    if (h.rung.inbox.ring().size() >= proto::kLinkTxCapacity)
        return Pass::Waiting;
    auto id = h.owner.intern_bytes(bytes, {}, hal::FabricAddr{});
    if (!id)
        return id.error().code == Errc::would_block ? Pass::Waiting : fail_(h);
    const proto::ActKey16 act = echo_wait_.mint(h.owner);
    if (!h.rung.inbox.push(
            proto::LinkOp::StagePayload{
                .payload = *id,
                .dest = proto::WideIoIndex{cores::PostNotify::kIndex},
                .copy_word = notify.copy_word(),
                .act = act
            }
        )) {
        return Pass::Waiting;
    }
    echo_wait_.arm(act, h.now_ns, notify.copy_bytes(), kEcho);
    landed_ = false;
    pc_ = Pc::Echo;
    return Pass::Stepped;
}

LoadWalk::Pass LoadWalk::skip_(const cores::TransferRow& r) {
    if (r.notify.armed && landed_) {
        pc_ = Pc::Notify;
        return Pass::Stepped;
    }
    next_row_();
    return Pass::Stepped;
}

LoadWalk::Pass LoadWalk::echo_(Host& h) {
    switch (echo_wait_.poll(h.rung.level, h.now_ns)) {
        case EchoWait::Answer::Waiting:
            return Pass::Waiting;
        case EchoWait::Answer::Answered:
            break;
        case EchoWait::Answer::Dropped:
            return fail_(h);
        case EchoWait::Answer::TimedOut:
            stalled_ = true;
            return fail_(h);
    }
    next_row_();
    return Pass::Stepped;
}

LoadWalk::Pass LoadWalk::save_(Host& h) {
    if (plan_.frame.save.empty()) {
        pc_ = Pc::Release;
        return Pass::Stepped;
    }
    if (save_due_ns_ == std::chrono::nanoseconds{})
        save_due_ns_ = h.now_ns + kSaveBoundNs;
    auto id = h.owner.intern_path(plan_.frame.save, proto::FileSize{});
    if (!id)
        return Pass::Waiting;
    const proto::ActKey g = h.owner.next_generation();
    if (!h.rung.inbox.push(
            proto::LinkOp::AttachSave{.slot = proto::SlotIndex{0}, .path = *id, .act = g}
        )) {
        return Pass::Waiting;
    }
    save_gen_ = g;
    pc_ = Pc::SaveWait;
    return Pass::Stepped;
}

LoadWalk::Pass LoadWalk::save_wait_(Host& h) {
    const cores::SaveExtent e = h.owner.save_extent();
    if (e.generation == save_gen_ && e.answer == cores::SaveAnswer::Known) {
        auto id = h.owner.intern_path(plan_.frame.save, e.size_bytes);
        if (!id)
            return Pass::Waiting;
        if (!h.rung.inbox.push(
                proto::LinkOp::BindSlot{
                    .slot = proto::SlotIndex{0},
                    .bind = proto::LinkOp::SlotBind::Mount,
                    .path = *id
                }
            )) {
            return Pass::Waiting;
        }
        pc_ = Pc::Release;
        return Pass::Stepped;
    }
    if (h.now_ns >= save_due_ns_) {
        pc_ = Pc::Release;
        return Pass::Stepped;
    }
    if (e.generation == save_gen_ && e.answer == cores::SaveAnswer::Declined) {
        pc_ = Pc::Save;
        return Pass::Stepped;
    }
    return Pass::Waiting;
}

LoadWalk::Pass LoadWalk::release_(Host& h) {

    const bool release = plan_.frame.release_reset || failed_;
    if (release && !status_bit0_(h, false))
        return Pass::Waiting;
    pc_ = Pc::Over;
    return failed_ ? Pass::Failed : Pass::Done;
}

LoadWalk::Pass LoadWalk::fail_(Host& h) {
    failed_ = true;
    ladder_.reset();
    pieces_.reset();
    if (plan_.frame.hold_reset) {
        pc_ = Pc::Release;
        return release_(h);
    }
    pc_ = Pc::Over;
    return Pass::Failed;
}

Rung::Pass LoadWalk::step(const RungEdges& edges, cores::ILadderHost& owner) {
    TASTY_SEAT_BODY(LoadWalk);
    Host h{
        .rung = LoadLadder::Host{edges.inbox, edges.level, edges.events},
        .owner = owner,
        .map = edges.window_map,
        .aperture = edges.aperture,
        .vfs = edges.vfs,
        .now_ns = edges.clock.now().time_since_epoch()
    };
    switch (step(h)) {
        case Pass::Stepped:
            return Rung::Pass::Moved;
        case Pass::Waiting:
            return Rung::Pass::Waiting;
        case Pass::Done:
            return Rung::Pass::Done;
        case Pass::Failed:
            return Rung::Pass::Failed;
    }
    return Rung::Pass::Failed;
}

}
