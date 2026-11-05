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

constexpr std::size_t kRows = 16;
constexpr std::size_t kOutputRows = kRows - 2;
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

bool ScriptRunPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(ScriptRunPage);
    (void)out.page.title.assign(runner_.title());
    out.title_arrow = OsdRenderer::kArrowNone;
    out.count = 0;

    const std::span<const std::string> lines = runner_.lines();
    const std::size_t line_count = lines.size();
    const std::size_t hd_count = line_count < app::PageDescription::kMaxRows
        ? line_count
        : app::PageDescription::kMaxRows;
    const std::size_t hd_first = line_count - hd_count;
    out.page.first = static_cast<std::uint16_t>(hd_first);
    out.page.total = static_cast<std::uint16_t>(line_count);

    std::string_view word = runner_.finished() ? std::string_view{"OK"} : policy_.running_footer();
    word.remove_prefix(std::min(word.find_first_not_of(' '), word.size()));
    std::string legend{"A "};
    legend += word;
    (void)out.page.footer.assign(legend);

    const bool notice = phase_ != Phase::Run;
    const std::size_t first = line_count > kOutputRows ? line_count - kOutputRows : 0;

    for (std::size_t i = 0; i < hd_count; ++i) {
        const std::size_t idx = hd_first + i;
        PageRow& r = out.rows[out.count++];
        r.has_hd = true;
        r.hd.kind = app::PageDescription::RowKind::Text;
        (void)r.hd.label.assign(lines[idx]);

        if (notice)
            continue;
        const std::size_t vr = idx - first;
        if (vr >= kOutputRows)
            continue;
        r.classic = classic_row(
            OsdRow::of(vr),
            lines[idx],
            false,
            false,
            std::nullopt,
            OsdRenderer::kArrowNone
        );
    }

    if (notice) {

        for (std::size_t i = 0; i < kRows; ++i) {
            const bool finish = i + 1 == kRows;
            out.rows[out.count++].classic = classic_row(
                OsdRow::of(i),
                finish
                    ? std::string_view{"           Finish"}
                    : (i == 7 ? std::string_view{"   Delete all pairings..."} : std::string_view{}),
                finish,
                false,
                std::nullopt,
                OsdRenderer::kArrowNone
            );
        }
        return true;
    }

    for (std::size_t i = 0; i < kOutputRows; ++i) {
        const std::size_t idx = first + i;
        if (idx < line_count && idx >= hd_first)
            continue;
        out.rows[out.count++].classic = classic_row(
            OsdRow::of(i),
            idx < line_count ? std::string_view{lines[idx]} : std::string_view{},
            false,
            false,
            std::nullopt,
            OsdRenderer::kArrowNone
        );
    }
    out.rows[out.count++].classic = classic_row(OsdRow::of(kOutputRows), std::string_view{});

    const std::string_view footer = runner_.finished() ? std::string_view{"             OK"}
                                                       : policy_.running_footer();
    out.rows[out.count++].classic = classic_row(OsdRow::of(kRows - 1), footer, true);
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
