// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/manifest_ladder.h"

#include <utility>
#include <variant>

#include "cores/addressed_load.h"
#include "proto/link_op.h"

namespace mister::app {

ManifestLadder::Pass ManifestLadder::step(Host& h) {
    switch (pc_) {
        case Pc::Next:
            return next_(h);
        case Pc::Window:
            return rung_(h);
        case Pc::Send:
            return send_(h);
        case Pc::Echo:
            return echo_(h);
        case Pc::Over:
            break;
    }
    return Pass::Failed;
}

bool ManifestLadder::wants_pass(const LinkTxChannel<SeatTag::Main>& inbox,
                                const FileTxLevelCell* level) const noexcept {
    switch (pc_) {
        case Pc::Next:
            return true;
        case Pc::Window:
            return load_ && load_->wants_pass(inbox, level);
        case Pc::Send:
            return inbox.ring().size() < proto::kLinkTxCapacity;
        case Pc::Echo: {
            const auto s = level != nullptr ? level->sample() : FileTxLevelCell::Sample{};
            return s && s.value.payload && s.value.act == act_;
        }
        case Pc::Over:
            break;
    }
    return false;
}

ManifestLadder::Pass ManifestLadder::next_(Host& h) {
    std::optional<cores::ManifestAct> act = acts_->next();
    if (!act) {
        if (const std::optional<Errc> why = acts_->refusal()) return fail_(*why);
        pc_ = Pc::Over;
        return Pass::Done;
    }
    if (const auto* w = std::get_if<cores::StageWindow>(&*act)) return window_(h, *w);
    bracket_.emplace(std::move(std::get<cores::SendBracket>(*act)));
    pc_ = Pc::Send;
    return Pass::Stepped;
}

ManifestLadder::Pass ManifestLadder::window_(Host& h, const cores::StageWindow& w) {
    const auto planned =
        cores::AddressedLoad::window_row(w.address, w.bytes, proto::IoIndex{w.index}, h.aperture);
    if (!planned) {
        refuse_window_(h, Errc::slot_range);
        return Pass::Stepped;
    }
    auto win =
        LoadWindow<seat_owner::Main>::open(h.map, h.aperture, os::PhysAddr{w.address}, w.bytes);
    if (!win) {
        refuse_window_(h, win.error().code);
        return Pass::Stepped;
    }
    cores::TransferRow r = *planned;
    r.bracket = cores::RowBracket::None;
    LoadLadder::Job job{.loader = std::make_unique<cores::AddressedLoad>(h.aperture),
                        .file = acts_->take_image(),
                        .total = w.bytes,
                        .wire_index = w.index,
                        .act = h.owner.next_generation(),
                        .row = r};
    auto l = LoadLadder::start(std::move(job), std::move(*win), rel_.view());
    if (!l) {
        refuse_window_(h, l.error().code);
        return Pass::Stepped;
    }
    load_.emplace(std::move(*l));
    pc_ = Pc::Window;
    return Pass::Stepped;
}

void ManifestLadder::refuse_window_(Host& h, Errc why) noexcept {
    ++h.windows.refusals;
    h.windows.refusal_code = static_cast<std::uint16_t>(why);
    pc_ = Pc::Next;
}

ManifestLadder::Pass ManifestLadder::rung_(Host& h) {
    const LoadLadder::Pass pass = load_->step(h.rung);
    if (pass != LoadLadder::Pass::Done && pass != LoadLadder::Pass::Failed) {
        return pass == LoadLadder::Pass::Waiting ? Pass::Waiting : Pass::Stepped;
    }
    load_.reset();
    if (pass == LoadLadder::Pass::Done) {
        ++h.windows.windows;
        acts_->mark_staged();
    } else {

        ++h.windows.refusals;
        h.windows.refusal_code = static_cast<std::uint16_t>(Errc::io);
        acts_->mark_torn();
    }
    pc_ = Pc::Next;
    return Pass::Stepped;
}

ManifestLadder::Pass ManifestLadder::send_(Host& h) {
    LinkTxChannel<SeatTag::Main>& inbox = h.rung.inbox;
    if (inbox.ring().size() >= proto::kLinkTxCapacity) return Pass::Waiting;
    cores::SendBracket& b = *bracket_;
    const std::uint64_t bytes = b.bytes.size();

    auto id = b.bytes.empty() ? inbox.intern_file_path(rel_.view(), 0, 0, b.aux)
                              : inbox.intern_file(std::move(b.bytes), {}, rel_.view(), 0);
    if (!id) return id.error().code == Errc::would_block ? Pass::Waiting : fail_(id.error().code);
    std::uint16_t act = 0;
    while (act == 0 || act == act_)
        act = static_cast<std::uint16_t>(h.owner.next_generation());
    const proto::LinkOp::StagePayload op{
        .payload = *id, .dest = proto::WideIoIndex{b.index}, .chunk = kSendChunk, .act = act};
    if (!inbox.push(op)) return fail_(Errc::would_block);
    act_ = act;
    bracket_.reset();
    echo_due_ns_ = h.now_ns + echo_bound_ns(bytes);
    pc_ = Pc::Echo;
    return Pass::Stepped;
}

ManifestLadder::Pass ManifestLadder::echo_(Host& h) {
    const auto s = h.rung.level != nullptr ? h.rung.level->sample() : FileTxLevelCell::Sample{};
    if (!s || !s.value.payload || s.value.act != act_) {
        return h.now_ns < echo_due_ns_ ? Pass::Waiting : fail_(Errc::timeout);
    }
    if (!s.value.open) return fail_(Errc::io);
    pc_ = Pc::Next;
    return Pass::Stepped;
}

ManifestLadder::Pass ManifestLadder::fail_(Errc why) noexcept {
    failure_ = why;
    load_.reset();
    bracket_.reset();
    pc_ = Pc::Over;
    return Pass::Failed;
}

}  // namespace mister::app
