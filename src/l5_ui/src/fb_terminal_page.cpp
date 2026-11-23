// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/fb_terminal_page.h"

#include <cstddef>
#include <iterator>
#include <string_view>
#include <utility>

#include "app/hd/page_description.h"
#include "ui/draw_classic.h"
#include "ui/osd_renderer.h"
#include "ui/page_context.h"
#include "ui/page_launches.h"
#include "ui/page_row.h"
#include "ui/page_walk.h"
#include "ui/script_launch.h"
#include "ui/ui_event.h"

namespace mister::ui {

namespace {

constexpr std::string_view kNagTitle{"Information"};

constexpr std::string_view kNagLines[] = {
    "",
    "",
    "",
    "",
    "  If you see this, then you",
    "  need to modify MiSTer.ini",
    "",
    " Either disable framebuffer:",
    "       fb_terminal=0",
    "",
    "  or enable scaler on VGA:",
    "       vga_scaler=1",
    "",
    "",
    "",
    "",
};

}

FbTerminalPage::FbTerminalPage(ScriptRequest request, infra::OptRef<FbTerminalHost> host) noexcept
    : request_(static_cast<ScriptRequest&&>(request)),
      script_(TerminalScript::for_user_script(request_.argv0())),
      host_(host) {}

FbTerminalPage::~FbTerminalPage() {

    if (host_ && current_seat() == SeatTag::Ui)
        host_->detach();
}

PageId FbTerminalPage::id() const {
    TASTY_SEAT_BODY(FbTerminalPage);
    return PageId::FbTerminal;
}

void FbTerminalPage::on_enter(PageContext&) {
    TASTY_SEAT_BODY(FbTerminalPage);
    if (!host_) {
        phase_ = TerminalPhase::Failed;
        refusals_.add();
        return;
    }
    const Ex<void> launched = host_->launch(script_);
    if (!launched)
        refusals_.add();
    phase_ = launched ? host_->phase() : TerminalPhase::Failed;
}

bool FbTerminalPage::walk(PageWalk& out) const {
    TASTY_SEAT_BODY(FbTerminalPage);
    (void)out.page.title.assign(kNagTitle);
    out.title_arrow = OsdRenderer::kArrowNone;
    out.count = 0;
    std::size_t line = 0;
    for (const RowIndex r : rows(kFullOsdRows)) {
        const std::string_view text = line < std::size(kNagLines) ? kNagLines[line] : "";
        out.rows[out.count++].classic = classic_row(osd_row_at(r.before()), text);
        ++line;
    }
    return true;
}

bool FbTerminalPage::describe(app::hd::PageDescription&) const {
    TASTY_SEAT_BODY(FbTerminalPage);
    return false;
}

Transition FbTerminalPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(FbTerminalPage);
    if (phase_ != TerminalPhase::Ended || !ev.raw.is_release())
        return Transition{};
    if (host_)
        host_->finish();
    return Transition::pop();
}

Transition FbTerminalPage::fall_back_(PageContext& ctx) {
    TASTY_SEAT_BODY(FbTerminalPage);
    if (!ctx.launches.script) {
        refusals_.add();
        return Transition::pop();
    }
    return ctx.launches.script->open(PageId::ScriptRun, request_, true);
}

Transition FbTerminalPage::on_tick(PageContext& ctx) {
    TASTY_SEAT_BODY(FbTerminalPage);
    if (phase_ == TerminalPhase::Failed)
        return fall_back_(ctx);
    if (!host_) {
        phase_ = TerminalPhase::Failed;
        return fall_back_(ctx);
    }
    const TerminalPhase was = phase_;
    phase_ = host_->phase();
    if (phase_ == TerminalPhase::Failed)
        return fall_back_(ctx);
    if (phase_ == TerminalPhase::Detached)
        return Transition::pop();

    if (phase_ == TerminalPhase::Idle && was != TerminalPhase::Idle)
        return Transition::pop();
    return Transition{};
}

MenuMask FbTerminalPage::mask() const {
    TASTY_SEAT_BODY(FbTerminalPage);
    return MenuMask{};
}

PageFacts FbTerminalPage::facts() const {
    TASTY_SEAT_BODY(FbTerminalPage);
    PageFacts f{};
    f.keys = phase_ == TerminalPhase::Running ? PageKeys::Terminal : PageKeys::Menu;
    f.allows_global_hotkeys = false;
    f.wake_every_round = phase_ != TerminalPhase::Ended;
    return f;
}

bool FbTerminalPage::holds_live_script() const {
    TASTY_SEAT_BODY(FbTerminalPage);
    return phase_ == TerminalPhase::Starting || phase_ == TerminalPhase::Running;
}

bool FbTerminalPage::runs_a_process() const {
    TASTY_SEAT_BODY(FbTerminalPage);
    return phase_ == TerminalPhase::Starting || phase_ == TerminalPhase::Running;
}

infra::Tally FbTerminalPage::refusals() const noexcept {
    TASTY_SEAT_BODY(FbTerminalPage);
    return refusals_;
}

}
