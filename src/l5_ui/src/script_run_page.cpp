// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/script_run_page.h"

#include <algorithm>
#include <span>
#include <string>

#include "app/page_description.h"
#include "ui/bt_ops.h"
#include "ui/menu_model.h"
#include "ui/osd_renderer.h"
#include "ui/script_launch.h"
#include "ui/script_policy.h"
#include "ui/page_context.h"
#include "ui/ui_event.h"

namespace mister::ui {

namespace {
constexpr std::uint32_t kRows = 16;
constexpr std::uint32_t kOutputRows = kRows - 2;
}

ScriptRunPage::ScriptRunPage(ScriptKind kind, std::string argv0, bool bt_reset) noexcept
    : kind_(kind),
      policy_(script_policy_for(kind)),
      argv0_(static_cast<std::string&&>(argv0)),
      bt_reset_(bt_reset) {}

bool ScriptRunPage::bt_pairing_active() const {
    TASTY_SEAT_BODY(ScriptRunPage);
    return policy_.is_pairing();
}

void ScriptRunPage::on_enter(PageContext& ctx) {
    TASTY_SEAT_BODY(ScriptRunPage);
    if (!runner_.start(kind_, argv0_, bt_reset_, ctx.bt))
        ++refusals_;
}

void ScriptRunPage::render(OsdRenderer& out) const {
    TASTY_SEAT_BODY(ScriptRunPage);
    out.set_title(runner_.title());

    if (phase_ != Phase::Run) {

        for (std::uint32_t i = 0; i < kRows - 1; ++i) {
            out.text(
                proto::OsdRow{static_cast<std::uint8_t>(i)},
                i == 7 ? std::string_view{"   Delete all pairings..."} : std::string_view{}
            );
        }
        out.text(proto::OsdRow{static_cast<std::uint8_t>(kRows - 1)}, "           Finish", true);
        return;
    }

    const auto lines = runner_.lines();
    const std::size_t total = lines.size();
    const std::size_t first = total > kOutputRows ? total - kOutputRows : 0;
    for (std::uint32_t i = 0; i < kOutputRows; ++i) {
        const std::size_t idx = first + i;
        out.text(
            proto::OsdRow{static_cast<std::uint8_t>(i)},
            idx < total ? std::string_view{lines[idx]} : std::string_view{}
        );
    }
    out.text(proto::OsdRow{static_cast<std::uint8_t>(kOutputRows)}, "");

    const std::string_view footer = runner_.finished() ? std::string_view{"             OK"}
                                                       : policy_.running_footer();
    out.text(proto::OsdRow{static_cast<std::uint8_t>(kRows - 1)}, footer, true);
}

bool ScriptRunPage::describe(app::PageDescription& out) const {
    TASTY_SEAT_BODY(ScriptRunPage);
    out = {};
    (void)out.title.assign(runner_.title());

    const std::span<const std::string> lines = runner_.lines();
    const std::uint16_t total = static_cast<std::uint16_t>(lines.size());
    constexpr std::uint16_t kMax = static_cast<std::uint16_t>(app::PageDescription::kMaxRows);
    const std::uint16_t count = total < kMax ? total : kMax;
    const std::uint16_t first = static_cast<std::uint16_t>(total - count);
    out.first = first;
    out.row_count = static_cast<std::uint8_t>(count);
    out.total = total;
    for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(count); ++i) {
        app::PageDescription::Row& row = out.rows[i];
        row.kind = app::PageDescription::RowKind::Text;
        (void)row.label.assign(lines[static_cast<std::size_t>(first) + i]);
    }
    std::string_view word = runner_.finished() ? std::string_view{"OK"} : policy_.running_footer();
    word.remove_prefix(std::min(word.find_first_not_of(' '), word.size()));
    std::string legend{"A "};
    legend += word;
    (void)out.footer.assign(legend);
    return true;
}

Transition ScriptRunPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(ScriptRunPage);
    if (phase_ == Phase::RenewPaint)
        return Transition{};
    if ((ev.buttons & (UiEvent::Select | UiEvent::Menu | UiEvent::Back)) == 0) {
        return Transition{};
    }

    if (phase_ == Phase::RenewDone)
        return exit_transition();
    if (runner_.finished())
        return Transition{};

    if (policy_.renews_on_back() && (ev.buttons & UiEvent::Back) != 0) {
        (void)runner_.cancel();
        phase_ = Phase::RenewPaint;
        return Transition{};
    }

    (void)runner_.cancel();
    return exit_transition();
}

Transition ScriptRunPage::on_tick(PageContext& ctx) {
    TASTY_SEAT_BODY(ScriptRunPage);
    if (phase_ == Phase::RenewPaint) {

        phase_ = Phase::RenewDone;
        if (!ctx.bt) {
            ++refusals_;
            return exit_transition();
        }
        if (!ctx.bt->renew_pairings())
            ++refusals_;
        if (ctx.launches.script) {
            ScriptLaunch::Request req;
            req.kind = ScriptKind::BtPair;
            req.bt_reset = bt_reset_;
            ctx.launches.script->pending = static_cast<ScriptLaunch::Request&&>(req);
            return Transition{Transition::Kind::Replace, PageId::ScriptRun};
        }
        ++refusals_;
        return exit_transition();
    }
    if (phase_ == Phase::RenewDone)
        return Transition{};
    if (runner_.finished())
        return Transition{};
    (void)runner_.pump();
    if (runner_.finished())
        return exit_transition();
    return Transition{};
}

Transition ScriptRunPage::exit_transition() const {
    return policy_.exit_transition();
}

}
