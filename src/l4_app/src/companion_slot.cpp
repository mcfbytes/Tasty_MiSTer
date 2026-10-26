// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/companion_slot.h"

#include <utility>

#include "cores/registry.h"
#include "infra/message_sum.h"

namespace mister::app {

void CompanionSlot::make() {
    TASTY_SEAT_BODY(CompanionSlot);
    withdraw();
    const auto hint = negotiated_.made_with_manifest() ? cores::LoadHint::XmlManifest
                                                       : cores::LoadHint::None;
    const auto row = cores::find_core(negotiated_.core_name().view(), hint);
    if (!row || (*row)->make_companion == nullptr)
        return;
    companion_ = (*row)->make_companion(vfs_);
    if (!binds_ || companion_ == nullptr)
        return;
    attached_ = binds_->push(
        infra::make<CompanionBind>(CompanionBind::Attach{.servant = companion_->servant()})
    );
    if (!attached_)
        ++tally_.companion_attach_refusals;
}

void CompanionSlot::withdraw() noexcept {
    TASTY_SEAT_BODY(CompanionSlot);
    if (companion_ == nullptr)
        return;
    if (attached_
        && !binds_->push(
            infra::make<CompanionBind>(CompanionBind::Attach{.servant = cores::ServantId::None})
        ))
        ++tally_.companion_attach_refusals;
    attached_ = false;
    companion_.reset();
}

Ex<CompanionWalk> CompanionSlot::plan(std::string_view path) {
    TASTY_SEAT_BODY(CompanionSlot);
    const cores::CompanionAsk ask{
        .path = path,
        .mailbox_live = attached_,
        .aperture_base = aperture_base_
    };
    auto plan = companion_->plan(ask);
    if (!plan)
        return std::unexpected(plan.error());
    gen_ = static_cast<std::uint16_t>(gen_ + 1u);
    if (gen_ == 0)
        gen_ = 1;
    return CompanionWalk::start(
        *plan,
        gen_,
        plan->loads ? companion_->loader() : nullptr,
        negotiated_.loaded_profile().windows,
        attached_ ? binds_ : infra::OptRef<CompanionHost::Binds>{}
    );
}

}
