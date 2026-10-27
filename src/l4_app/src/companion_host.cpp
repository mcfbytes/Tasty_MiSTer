// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/companion_host.h"

#include <cstddef>
#include <optional>
#include <utility>

namespace mister::app {

void CompanionHost::install(cores::ServantSet set) noexcept {
    servants_ = std::move(set);
}

void CompanionHost::serve() noexcept {
    TASTY_SEAT_BODY(CompanionHost);
    while (const std::optional<CompanionBind> m = binds_.pop())
        infra::dispatch<infra::AllRouted>(*m, *this);
}

void CompanionHost::on(const CompanionBind::Attach& a) noexcept {
    TASTY_SEAT_BODY(CompanionHost);
    if (held_)
        held_->release();
    const auto i = static_cast<std::size_t>(a.servant);
    held_ = infra::opt_ref_of(i < servants_.size() ? servants_[i].get() : nullptr);
    if (held_)
        held_->begin_core();
    bound_gen_ = 0;
    attaches_.add();
}

void CompanionHost::on(const CompanionBind::Bind& b) noexcept {
    TASTY_SEAT_BODY(CompanionHost);
    if (!held_) {
        unheld_.add();
        return;
    }
    held_->rebind(b.stem.view(), b.present);
    bound_gen_ = b.gen;
    applied_.add();
}

void CompanionHost::misrouted(const CompanionBind&) noexcept {
    TASTY_SEAT_BODY(CompanionHost);
}

infra::OptRef<cores::IMailboxServant> CompanionHost::servant_for(std::uint16_t gen) noexcept {
    TASTY_SEAT_BODY(CompanionHost);
    if (gen != bound_gen_ || !held_)
        serve();
    if (gen == bound_gen_ && gen != 0)
        return held_;
    return {};
}

void CompanionHost::refill_one() noexcept {
    TASTY_SEAT_BODY(CompanionHost);
    if (held_)
        held_->refill_one();
}

bool CompanionHost::rest() const noexcept {
    return binds_.empty() && (!held_ || held_->rest());
}

void CompanionHost::release() noexcept {
    TASTY_SEAT_BODY(CompanionHost);
    if (held_)
        held_->release();
}

}
