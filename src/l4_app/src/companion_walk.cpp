// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/companion_walk.h"

#include "proto/link_op.h"

namespace mister::app {

Ex<CompanionWalk> CompanionWalk::start(const cores::CompanionPlan& plan, std::uint16_t gen,
                                       std::unique_ptr<cores::ILoader> loader) {
    if (gen == 0 || (plan.loads && loader == nullptr))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    CompanionWalk w(plan, gen, std::move(loader));
    if (!w.stem_.assign(plan.stem) || !w.source_.assign(plan.row.source))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});
    w.plan_.stem = {};
    w.plan_.row.source = {};
    return w;
}

bool CompanionWalk::wants_pass(const LinkTxChannel& inbox,
                               const FileTxLevelCell* level) const noexcept {
    switch (pc_) {
        case Pc::Over:
            return false;
        case Pc::Load:
            return !walk_ || walk_->wants_pass(inbox, level);
        default:
            return inbox.ring().size() < proto::kLinkTxCapacity;
    }
}

CompanionWalk::Pass CompanionWalk::step(Host& h) {
    switch (pc_) {
        case Pc::Bind:
            return bind_(h);
        case Pc::Before:
            return write_(h, plan_.before, proto::MailboxPoll::Off, Pc::Load);
        case Pc::Load:
            return load_(h);
        case Pc::After:
            return write_(h, plan_.after, plan_.poll, Pc::Over);
        case Pc::Over:
            break;
    }
    return Pass::Done;
}

CompanionWalk::Pass CompanionWalk::bind_(Host& h) {
    if (h.binds != nullptr) {
        const CompanionBind m = infra::make<CompanionBind>(
            CompanionBind::Bind{.stem = stem_, .gen = gen_, .present = plan_.present});
        if (!h.binds->push(m)) return Pass::Waiting;
        bound_ = true;
    }
    pc_ = plan_.loads ? Pc::Before : Pc::After;
    return Pass::Stepped;
}

CompanionWalk::Pass CompanionWalk::write_(Host& h, const std::array<std::uint16_t, 3>& words,
                                          proto::MailboxPoll poll, Pc next) {
    const proto::LinkOp::MailboxWrite op{
        .opcode = plan_.opcode, .poll = poll, .gen = gen_, .words = words};
    if (!h.load.rung.inbox.push(op)) return Pass::Waiting;
    pc_ = next;
    return next == Pc::Over ? Pass::Done : Pass::Stepped;
}

CompanionWalk::Pass CompanionWalk::load_(Host& h) {
    if (!walk_) {
        cores::LoadPlan p{};
        p.rows[0] = plan_.row;
        p.rows[0].source = source_.view();
        p.count = 1;
        auto w = LoadWalk::start(std::move(loader_), p);
        if (!w) {
            pc_ = Pc::After;
            return Pass::Stepped;
        }
        walk_.emplace(std::move(*w));
    }
    const LoadWalk::Pass pass = walk_->step(h.load);
    if (pass == LoadWalk::Pass::Stepped) return Pass::Stepped;
    if (pass == LoadWalk::Pass::Waiting) return Pass::Waiting;
    loaded_ = pass == LoadWalk::Pass::Done;
    walk_.reset();
    pc_ = Pc::After;
    return Pass::Stepped;
}

}  // namespace mister::app
