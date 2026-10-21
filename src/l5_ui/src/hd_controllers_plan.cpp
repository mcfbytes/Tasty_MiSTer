// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_controllers_plan.h"

#include <string>

#include "app/identity_latch.h"
#include "app/session_identity.h"
#include "ui/hd_plan_inputs.h"
#include "ui/page_context.h"

namespace mister::ui {
namespace {

std::string core_name_of(const HdPlanInputs& in) {
    if (!in.ctx.identity)
        return {};
    app::SessionIdentity id{};
    if (!in.ctx.identity->copy(id))
        return {};
    return std::string(id.core);
}

std::string define_label_of(const HdPlanInputs& in) {
    const std::string name = in.front_end ? std::string{} : core_name_of(in);
    return name.empty() ? std::string("Define joystick buttons") : "Define " + name + " buttons";
}

}

HdControllersPlan::HdControllersPlan(const HdPlanInputs& in)
    : pair_(in.ctx),
      define_label_(define_label_of(in)),
      define_(HdPushRow::Spec{.label = define_label_, .page = PageId::JoyMap}) {
    TASTY_SEAT_BODY(HdControllersPlan);
    refs_ = {pair_, define_};

    if (!in.ctx.launches.script)
        ++refusals_;
}

PageId HdControllersPlan::id() const {
    TASTY_SEAT_BODY(HdControllersPlan);
    return PageId::HdControllers;
}

std::string_view HdControllersPlan::title() const {
    TASTY_SEAT_BODY(HdControllersPlan);
    return "Controllers";
}

std::span<const std::reference_wrapper<IHdRow>> HdControllersPlan::rows() const {
    TASTY_SEAT_BODY(HdControllersPlan);
    return refs_;
}

std::uint32_t HdControllersPlan::refusals() const noexcept {
    TASTY_SEAT_BODY(HdControllersPlan);
    return refusals_;
}

}
