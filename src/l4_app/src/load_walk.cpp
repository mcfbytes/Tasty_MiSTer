// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/load_walk.h"

#include <algorithm>
#include <vector>

#include "app/load_window.h"
#include "cores/transfer_row.h"
#include "proto/link_op.h"

namespace mister::app {

Ex<LoadWalk> LoadWalk::start(std::unique_ptr<cores::ILoader> loader, const cores::LoadPlan& plan) {
    if (loader == nullptr || plan.count > plan.rows.size() ||
        (plan.count == 0 && !plan.frame.hold_reset)) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    for (std::size_t i = 0; i < plan.count; ++i) {
        if (plan.rows[i].dest == cores::RowDest::Fio)
            return std::unexpected(
                Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(i)});
    }
    return LoadWalk(std::move(loader), plan);
}

bool LoadWalk::wants_pass(const LinkTxChannel& inbox, const FileTxLevelCell* level) const noexcept {
    switch (pc_) {
        case Pc::Over:
            return false;
        case Pc::Rung:
            return ladder_ && ladder_->wants_pass(inbox, level);
        case Pc::Echo: {
            if (level == nullptr) return false;
            const auto s = level->sample();
            return s && s.value.payload && s.value.act == act_;
        }
        case Pc::SaveWait:
            return false;
        case Pc::Pieces:
            return inbox.ring().size() < cores::PayloadPieces::kAhead;
        default:
            return inbox.ring().size() < proto::kLinkTxCapacity;
    }
}

LoadWalk::Pass LoadWalk::step(Host& h) {
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
    return h.rung.inbox.push(proto::LinkOp::WriteStatus{
        .start = proto::StatusBit{0}, .width = 1, .value = asserted ? 1u : 0u});
}

LoadWalk::Pass LoadWalk::hold_(Host& h) {
    if (plan_.frame.hold_reset && !status_bit0_(h, true)) return Pass::Waiting;
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
    if (h.map == nullptr) return fail_(h);

    std::unique_ptr<svc::IFile> file;
    if (!r.source.empty() || r.file_len != 0) {
        auto f = h.vfs.open(r.source, svc::OpenMode::ReadWhole);
        if (!f) return skip_(r);
        file = std::move(*f);
    }
    const auto phys = cores::row_address(r, h.windows, h.aperture.phys.v);
    if (!phys) return fail_(h);
    const std::uint64_t extent = r.extent != 0 ? r.extent : std::uint64_t{r.file_len} * r.stride;
    auto w = LoadWindow::open(*h.map, h.aperture, os::PhysAddr{*phys}, extent);
    if (!w) return fail_(h);
    std::optional<LoadWindow> mirror{};
    if (r.mirror != 0) {
        auto m = LoadWindow::open(*h.map, h.aperture, os::PhysAddr{r.mirror}, extent);
        if (!m) return fail_(h);
        mirror.emplace(std::move(*m));
    }
    cores::TransferRow quiet = r;
    quiet.notify = {};

    const std::uint32_t act =
        r.bracket == cores::RowBracket::AroundWithLength ? h.owner.next_generation() : 0u;
    auto l = LoadLadder::start(LoadLadder::Job{.loader = std::move(loader_),
                                               .file = std::move(file),
                                               .total = r.file_len,
                                               .wire_index = r.index.v,
                                               .act = act,
                                               .row = quiet,
                                               .mirror = std::move(mirror)},
                               std::move(*w), r.source);
    if (!l) return fail_(h);
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
    if (pass == LoadLadder::Pass::Failed) return fail_(h);
    landed_ = true;
    const cores::TransferRow& r = plan_.rows[row_];
    if (!r.notify.armed) {
        next_row_();
        return Pass::Stepped;
    }
    pc_ = Pc::Row;
    const Pass sent = notify_(h, r.notify);
    if (sent == Pass::Waiting) {

        plan_.rows[row_].dest = cores::RowDest::Notify;
    }
    return sent == Pass::Waiting ? Pass::Stepped : sent;
}

LoadWalk::Pass LoadWalk::payload_(Host& h, const cores::TransferRow& r) {
    auto file = h.vfs.open(r.source, svc::OpenMode::ReadWhole);
    if (!file) return skip_(r);
    std::vector<std::uint8_t> raw(r.file_len);
    std::size_t got = 0;
    while (got < raw.size()) {
        auto n = (*file)->read_at(r.file_offset + got,
                                  std::as_writable_bytes(std::span(raw).subspan(got)));
        if (!n || *n == 0) break;
        got += *n;
    }
    raw.resize(std::max<std::size_t>(got, r.extent), std::uint8_t{0});
    std::uint16_t chunk = kPayloadChunk;
    if (r.byte_words) {

        std::vector<std::uint8_t> wide(2u * raw.size(), std::uint8_t{0});
        for (std::size_t i = 0; i < raw.size(); ++i)
            wide[2u * i] = raw[i];
        raw = std::move(wide);
        chunk = static_cast<std::uint16_t>(2u * kPayloadChunk);
    }
    const std::uint64_t bytes = raw.size();
    auto p = cores::PayloadPieces::of_bytes(
        std::move(raw), {},
        {.dest = proto::WideIoIndex{r.index.v}, .chunk = chunk, .act = next_act_(h)});
    if (!p) return fail_(h);
    pieces_.emplace(std::move(*p));
    echo_bytes_ = bytes;
    pc_ = Pc::Pieces;
    return pieces_step_(h);
}

LoadWalk::Pass LoadWalk::pieces_step_(Host& h) {
    const cores::PayloadPieces::Pass pass = pieces_->step(h.owner);
    if (pass == cores::PayloadPieces::Pass::Waiting) return Pass::Waiting;
    pieces_.reset();
    if (pass == cores::PayloadPieces::Pass::Failed) return fail_(h);
    echo_due_ns_ = h.now_ns + echo_bound_ns(echo_bytes_);
    pc_ = Pc::Echo;
    return Pass::Stepped;
}

std::uint16_t LoadWalk::next_act_(Host& h) {
    std::uint16_t act = 0;
    while (act == 0 || act == act_)
        act = static_cast<std::uint16_t>(h.owner.next_generation());
    act_ = act;
    return act;
}

LoadWalk::Pass LoadWalk::notify_(Host& h, const cores::PostNotify& n) {
    std::vector<std::uint8_t> words;
    words.reserve(2u * n.words.size());
    for (const std::uint16_t w : n.words) {
        words.push_back(static_cast<std::uint8_t>(w & 0xFFu));
        words.push_back(static_cast<std::uint8_t>(w >> 8));
    }
    return send_(h, words, n);
}

LoadWalk::Pass LoadWalk::send_(Host& h, std::span<const std::uint8_t> bytes,
                               const cores::PostNotify& notify) {
    if (h.rung.inbox.ring().size() >= proto::kLinkTxCapacity) return Pass::Waiting;
    auto id = h.owner.intern_bytes(bytes, {}, 0);
    if (!id) return id.error().code == Errc::would_block ? Pass::Waiting : fail_(h);
    const std::uint16_t prev = act_;
    const std::uint16_t act = next_act_(h);
    if (!h.rung.inbox.push(proto::LinkOp::StagePayload{
            .payload = *id,
            .dest = proto::WideIoIndex{cores::PostNotify::kIndex},
            .copy_word = notify.copies() ? cores::PostNotify::kCopyWord : std::uint8_t{0},
            .act = act})) {
        act_ = prev;
        return Pass::Waiting;
    }
    echo_due_ns_ = h.now_ns + echo_bound_ns(notify.copy_bytes());
    landed_ = false;
    pc_ = Pc::Echo;
    return Pass::Stepped;
}

LoadWalk::Pass LoadWalk::skip_(const cores::TransferRow& r) {
    if (r.notify.armed && landed_) {
        plan_.rows[row_].dest = cores::RowDest::Notify;
        pc_ = Pc::Row;
        return Pass::Stepped;
    }
    next_row_();
    return Pass::Stepped;
}

LoadWalk::Pass LoadWalk::echo_(Host& h) {
    const auto s = h.rung.level != nullptr ? h.rung.level->sample() : FileTxLevelCell::Sample{};
    if (!s || !s.value.payload || s.value.act != act_) {
        if (h.now_ns < echo_due_ns_) return Pass::Waiting;
        stalled_ = true;
        return fail_(h);
    }
    if (!s.value.open) return fail_(h);
    next_row_();
    return Pass::Stepped;
}

LoadWalk::Pass LoadWalk::save_(Host& h) {
    if (plan_.frame.save.empty()) {
        pc_ = Pc::Release;
        return Pass::Stepped;
    }
    if (save_due_ns_ == 0) save_due_ns_ = h.now_ns + kSaveBoundNs;
    auto id = h.owner.intern_path(plan_.frame.save, 0);
    if (!id || *id == proto::FileId{}) return Pass::Waiting;
    const std::uint32_t g = h.owner.next_generation();
    if (!h.rung.inbox.push(proto::LinkOp::BindSlot{.slot = proto::SlotIndex{0},
                                                   .bind = proto::LinkOp::SlotBind::Attach,
                                                   .path = *id,
                                                   .act_gen = g})) {
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
        if (!id || *id == proto::FileId{}) return Pass::Waiting;
        if (!h.rung.inbox.push(proto::LinkOp::BindSlot{.slot = proto::SlotIndex{0},
                                                       .bind = proto::LinkOp::SlotBind::Mount,
                                                       .path = *id})) {
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
    if (plan_.frame.release_reset && !status_bit0_(h, false)) return Pass::Waiting;
    pc_ = Pc::Over;
    return failed_ ? Pass::Failed : Pass::Done;
}

LoadWalk::Pass LoadWalk::fail_(Host& h) {
    failed_ = true;
    ladder_.reset();
    pieces_.reset();
    if (plan_.frame.hold_reset) {
        pc_ = Pc::Release;
        plan_.frame.release_reset = true;
        return release_(h);
    }
    pc_ = Pc::Over;
    return Pass::Failed;
}

}  // namespace mister::app
