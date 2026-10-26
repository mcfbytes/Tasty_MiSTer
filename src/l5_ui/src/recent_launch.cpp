// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/recent_launch.h"

#include <array>

#include "app/cmd_verb_sink.h"
#include "app/mgl_doc.h"
#include "app/session_identity.h"
#include "infra/ascii.h"
#include "ui/confirm_launch.h"
#include "ui/confirm_page.h"
#include "ui/hd_page0_walk.h"
#include "ui/game_title.h"
#include "ui/hd_plan_inputs.h"
#include "ui/now_playing.h"
#include "ui/options_page.h"
#include "ui/page_context.h"
#include "ui/slot_focus.h"

namespace mister::ui {
namespace {

constexpr std::array<std::string_view, 2> kYesNo{"Yes", "No"};

app::Mgl document_of(const app::RecentGames::Record& r) {
    app::Mgl doc;
    (void)doc.rbf.assign(r.rbf);
    (void)doc.path.assign(r.path);
    doc.slot = r.slot == 'S' ? app::Mgl::Slot::Image : app::Mgl::Slot::File;
    doc.index = r.ioctl_index;
    doc.delay_s = 0;
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
        ++refusals_;
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
    return page0_.slot_row(
        SlotFocus{
            r.slot == 'S' ? SlotFocus::Kind::Image : SlotFocus::Kind::File,
            proto::IoIndex{r.ioctl_index}
        }
    );
}

Transition RecentLaunch::launch(const app::RecentGames::Record& r) {
    TASTY_SEAT_BODY(RecentLaunch);

    if (const infra::OptRef<const OptionItem> slot = slot_for_(r); slot && picks_) {
        const std::string title{game_title(r.path)};
        return picks_->pick(*slot, r.path, title);
    }

    if (!ctx_.route || !ctx_.route->on(document_of(r))) {
        ++refusals_;
        return Transition{};
    }
    ++published_;
    return Transition{Transition::Kind::CloseAll};
}

bool RecentLaunch::ask(
    IConfirmHost& host,
    std::string_view title,
    std::span<const std::string_view> body
) {
    TASTY_SEAT_BODY(RecentLaunch);
    if (!ctx_.launches.confirm) {
        ++refusals_;
        return false;
    }
    ConfirmPage::Spec spec{};
    spec.title = title;
    spec.body = body;
    spec.choices = kYesNo;
    spec.host = infra::OptRef<IConfirmHost>{host};
    spec.initial = 1;
    ctx_.launches.confirm->pending = spec;
    return true;
}

std::uint32_t RecentLaunch::refusals() const noexcept {
    TASTY_SEAT_BODY(RecentLaunch);
    return refusals_ + (picks_ ? picks_->refusals() : 0u);
}

std::uint32_t RecentLaunch::commands_published() const noexcept {
    TASTY_SEAT_BODY(RecentLaunch);
    return published_ + (picks_ ? picks_->commands_published() : 0u);
}

}
