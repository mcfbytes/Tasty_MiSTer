// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/recent_launch.h"

#include <array>
#include <optional>

#include "app/cmd_verb_sink.h"
#include "app/mgl.h"
#include "app/session_identity.h"
#include "infra/ascii.h"
#include "proto/types.h"
#include "ui/confirm_launch.h"
#include "ui/confirm_page.h"
#include "ui/hd_page0_walk.h"
#include "ui/hd_plan_inputs.h"
#include "ui/now_playing.h"
#include "ui/option_item.h"
#include "ui/page_context.h"
#include "ui/slot_focus.h"

namespace mister::ui {
namespace {

constexpr std::array<std::string_view, 2> kYesNo{"Yes", "No"};

static_assert(
    kYesNo[RecentLaunch::row_of(RecentLaunch::YesNo::Yes).v] == "Yes"
    && kYesNo[RecentLaunch::row_of(RecentLaunch::YesNo::No).v] == "No"
);

std::optional<app::Mgl> document_of(const app::RecentGames::Record& r) {
    const std::optional<proto::FileSlotDigit> digit = proto::FileSlotDigit::from(r.row.index);
    if (!digit)
        return std::nullopt;
    app::Mgl doc;
    (void)doc.rbf.assign(r.rbf);
    (void)doc.path.assign(r.path);
    doc.slot = r.row.kind;
    doc.index = *digit;
    doc.delay_s = app::DelaySeconds{};
    return doc;
}

}

RecentLaunch::RecentLaunch(const HdPlanInputs& in)
    : ctx_(in.ctx),
      running_core_(app::recent_core_name(in.now_playing.core_image())),
      env_(HdPage0Walk::page0_of(in.env)),
      front_end_(in.front_end) {
    TASTY_SEAT_BODY(RecentLaunch);
    if (front_end_)
        return;
    if (!page0_.run(in.items, in.env, true, in.verbs)) {
        refusals_.add();
        return;
    }
    picks_.emplace(
        env_,
        SlotPickHost::Borrows{
            .asks = ctx_.ui_requests,
            .browser = ctx_.launches.browser,
            .recents = ctx_.recents,
            .names = ctx_.names,
            .info = ctx_.info,
            .files = ctx_.files
        }
    );
}

bool RecentLaunch::asks_first(const app::RecentGames::Record& r) const {
    TASTY_SEAT_BODY(RecentLaunch);
    if (front_end_)
        return false;
    return !(slot_for_(r) && picks_);
}

infra::OptRef<const OptionItem> RecentLaunch::slot_for_(const app::RecentGames::Record& r) const {
    if (!infra::ieq(r.core, running_core_))
        return {};
    return page0_.slot_row(r.row);
}

Transition RecentLaunch::launch(const app::RecentGames::Record& r) {
    TASTY_SEAT_BODY(RecentLaunch);

    if (const infra::OptRef<const OptionItem> slot = slot_for_(r); slot && picks_) {
        return picks_->pick(*slot, r.path);
    }

    const std::optional<app::Mgl> doc = document_of(r);
    if (!doc)
        return Transition::close_all();
    if (!ctx_.route || !ctx_.route->on(*doc)) {
        refusals_.add();
        return Transition{};
    }
    ++published_;
    return Transition::close_all();
}

bool RecentLaunch::ask(
    IConfirmHost& host,
    std::string_view title,
    std::span<const std::string_view> body
) {
    TASTY_SEAT_BODY(RecentLaunch);
    if (!ctx_.launches.confirm) {
        refusals_.add();
        return false;
    }
    ConfirmPage::Spec spec{};
    spec.title = title;
    spec.body = body;
    spec.choices = kYesNo;
    spec.host = infra::OptRef<IConfirmHost>{host};
    spec.initial = row_of(YesNo::No);
    ctx_.launches.confirm->pending = spec;
    return true;
}

svc::Tally RecentLaunch::refusals() const noexcept {
    TASTY_SEAT_BODY(RecentLaunch);
    return refusals_.sum(picks_ ? picks_->refusals() : svc::Tally{});
}

std::uint32_t RecentLaunch::commands_published() const noexcept {
    TASTY_SEAT_BODY(RecentLaunch);
    return published_ + (picks_ ? picks_->commands_published() : 0u);
}

}
