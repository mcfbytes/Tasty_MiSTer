// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/script_run_page.h"

#include <algorithm>
#include <span>
#include <string>

#include "app/page_description.h"
#include "ui/bt_ops.h"
#include "ui/draw_classic.h"
#include "ui/menu_model.h"
#include "ui/osd_renderer.h"
#include "ui/page_row.h"
#include "ui/page_walk.h"
#include "ui/script_launch.h"
#include "ui/script_policy.h"
#include "ui/page_context.h"
#include "ui/ui_event.h"

namespace mister::ui {

namespace {

constexpr RowCount kLegendRows{2};
constexpr RowCount kOutputRows = kFullOsdRows - kLegendRows;
constexpr proto::OsdRow kRenewNoticeRow{7};
}

ScriptRunPage::ScriptRunPage(ScriptRequest request) noexcept
    : policy_(script_policy_for(request.kind())),
      request_(static_cast<ScriptRequest&&>(request)) {}

bool ScriptRunPage::bt_pairing_active() const {
    TASTY_SEAT_BODY(ScriptRunPage);
    return policy_.is_pairing();
}

void ScriptRunPage::on_enter(PageContext& ctx) {
    TASTY_SEAT_BODY(ScriptRunPage);
    if (!runner_.start(request_, ctx.bt))
        ++refusals_;
}

bool ScriptRunPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(ScriptRunPage);
    (void)out.page.title.assign(runner_.title());
    out.title_arrow = OsdRenderer::kArrowNone;
    out.count = 0;

    const RowSpan<const std::string> lines{runner_.lines()};
    const RowCount line_count = lines.size();
    const RowCount hd_count = std::min(line_count, RowCount::of(app::PageDescription::kMaxRows));
    const RowIndex hd_first = row_after(line_count - hd_count);
    out.page.first = hd_first;
    out.page.total = line_count;

    std::string_view word = runner_.finished() ? std::string_view{"OK"} : policy_.running_footer();
    word.remove_prefix(std::min(word.find_first_not_of(' '), word.size()));
    std::string legend{"A "};
    legend += word;
    (void)out.page.footer.assign(legend);

    const bool notice = phase_ != Phase::Run;
    const RowCount first = saturating_sub(line_count, kOutputRows);

    for (const RowIndex i : rows(hd_count)) {
        const RowIndex idx = hd_first + i.before();
        PageRow& r = out.rows[out.count++];
        r.has_hd = true;
        r.hd.kind = app::PageDescription::RowKind::Text;
        (void)r.hd.label.assign(lines[idx]);

        if (notice)
            continue;
        const RowCount vr = idx - row_after(first);
        if (vr >= kOutputRows)
            continue;
        r.classic = classic_row(osd_row_at(vr), lines[idx], {.arrow = OsdRenderer::kArrowNone});
    }

    if (notice) {

        for (const RowIndex r : rows(kFullOsdRows)) {
            const proto::OsdRow at = osd_row_at(r.before());
            const bool finish = at == last_osd_row(kFullOsdRows);
            out.rows[out.count++].classic = classic_row(
                at,
                finish ? std::string_view{"           Finish"}
                       : (at == kRenewNoticeRow ? std::string_view{"   Delete all pairings..."}
                                                : std::string_view{}),
                {.inverted = finish, .arrow = OsdRenderer::kArrowNone}
            );
        }
        return true;
    }

    for (const RowIndex r : rows(kOutputRows)) {
        const RowIndex idx = row_after(first) + r.before();
        const bool listed = idx < row_after(line_count);
        if (listed && idx >= hd_first)
            continue;
        out.rows[out.count++].classic = classic_row(
            osd_row_at(r.before()),
            listed ? std::string_view{lines[idx]} : std::string_view{},
            {.arrow = OsdRenderer::kArrowNone}
        );
    }
    out.rows[out.count++].classic = classic_row(osd_row_at(kOutputRows), std::string_view{});

    const std::string_view footer = runner_.finished() ? std::string_view{"             OK"}
                                                       : policy_.running_footer();
    out.rows[out.count++]
        .classic = classic_row(last_osd_row(kFullOsdRows), footer, {.inverted = true});
    return true;
}

Transition ScriptRunPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(ScriptRunPage);
    if (phase_ == Phase::RenewPaint)
        return Transition{};
    if (!ev.buttons.has_any({UiEvent::Select | UiEvent::Menu | UiEvent::Back})) {
        return Transition{};
    }

    if (phase_ == Phase::RenewDone)
        return exit_transition();
    if (runner_.finished())
        return Transition{};

    if (policy_.renews_on_back() && ev.buttons.has_any({UiEvent::Back})) {
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

            ctx.launches.script->pending = request_;
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
