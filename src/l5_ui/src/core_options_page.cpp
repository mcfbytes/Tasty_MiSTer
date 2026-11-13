// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/core_options_page.h"

#include <memory>
#include <utility>

#include "ui/page_ask.h"
#include "ui/page_context.h"
#include "ui/ui_event.h"

namespace mister::ui {

CoreOptionsPage::CoreOptionsPage(
    const proto::ItemTable& table,
    const BuildEnv& env,
    std::string_view title,
    infra::OptRef<const StatusView> status
)
    : title_(title),
      env_(env),
      status_(status) {
    auto classic = std::make_unique<ClassicOptionsView>(static_cast<IOptionsSurface&>(*this));
    classic_ = infra::OptRef<ClassicOptionsView>{*classic};
    view_ = std::move(classic);
    first_walk_(table, env);
}

CoreOptionsPage::CoreOptionsPage(
    const proto::ItemTable& table,
    const BuildEnv& env,
    std::string_view title,
    infra::OptRef<const StatusView> status,
    const HdOptionsView::Spec& hd
)
    : title_(title),
      env_(env),
      status_(status),
      view_(std::make_unique<HdOptionsView>(static_cast<IOptionsSurface&>(*this), hd)) {
    first_walk_(table, env);
}

CoreOptionsPage::~CoreOptionsPage() = default;

void CoreOptionsPage::first_walk_(const proto::ItemTable& table, const BuildEnv& env) {
    TASTY_SEAT_BODY(CoreOptionsPage);
    model_.build(table, view_->walk_env(env));
    view_->on_model_rebuilt();
}

void CoreOptionsPage::rebuild(const proto::ItemTable& table, const BuildEnv& env) {
    TASTY_SEAT_BODY(CoreOptionsPage);
    env_ = SlotPickHost::Env{env};
    model_.build(table, view_->walk_env(env));
    view_->on_model_rebuilt();
}

void CoreOptionsPage::on_enter(PageContext& ctx) {
    TASTY_SEAT_BODY(CoreOptionsPage);
    asks_ = ctx.ui_requests;
    link_tx_ = ctx.link_tx;

    if (picks_) {
        refusals_.add(picks_->refusals());
        published_ += picks_->commands_published();
    }
    picks_.emplace(
        env_,
        SlotPickHost::Borrows{
            .asks = ctx.ui_requests,
            .browser = ctx.launches.browser,
            .recents = ctx.recents,
            .names = ctx.names,
            .info = ctx.info,
            .files = ctx.files
        }
    );
}

std::string CoreOptionsPage::compose(const OptionItem& it) const {
    return it.compose(OptionItem::Look{.flat = model_.flat(), .value = status_field(it.bits)});
}

Transition CoreOptionsPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(CoreOptionsPage);
    return view_->handle(ev);
}

std::string_view CoreOptionsPage::heading() const {
    TASTY_SEAT_BODY(CoreOptionsPage);
    return title_;
}

proto::FieldValue CoreOptionsPage::field(BitField bits) const {
    TASTY_SEAT_BODY(CoreOptionsPage);
    return status_field(bits);
}

StatusView CoreOptionsPage::live_words() const {
    TASTY_SEAT_BODY(CoreOptionsPage);
    return status_ ? *status_ : StatusView{};
}

Transition CoreOptionsPage::open_recents(const OptionItem& it) {
    TASTY_SEAT_BODY(CoreOptionsPage);
    if (!picks_) {
        refusals_.add();
        return Transition{};
    }
    return picks_->open_recents(it);
}

void CoreOptionsPage::save_config() {
    TASTY_SEAT_BODY(CoreOptionsPage);
    if (!push_ask(asks_, app::UiRequest::SaveConfig{})) {
        refusals_.add();
        return;
    }
    ++published_;
}

void CoreOptionsPage::refuse() {
    TASTY_SEAT_BODY(CoreOptionsPage);
    refusals_.add();
}

void CoreOptionsPage::write_status(BitField bits, proto::FieldValue value) {
    TASTY_SEAT_BODY(CoreOptionsPage);
    push_op(proto::LinkOp::WriteStatus{.field = bits, .value = value});
}

void CoreOptionsPage::toggle(BitField bits, bool ex) {
    TASTY_SEAT_BODY(CoreOptionsPage);
    const app::UiRequest::ResetCore ask{
        .edge = proto::ResetEdge::osd_toggle(bits.start, ex),
        .scope = app::CoreScope{env_.conf_gen}
    };
    if (!push_ask(asks_, ask)) {
        refusals_.add();
        return;
    }
    ++published_;
}

template <class A>
void CoreOptionsPage::push_op(const A& op) {
    if (!link_tx_ || !link_tx_->push(op)) {
        refusals_.add();
        return;
    }
    ++published_;
}

Transition CoreOptionsPage::open_browser(const OptionItem& it) {
    TASTY_SEAT_BODY(CoreOptionsPage);
    if (!picks_) {
        refusals_.add();
        return Transition{};
    }
    return picks_->open(it);
}

PageId CoreOptionsPage::subpage_page() const {
    TASTY_SEAT_BODY(CoreOptionsPage);
    return id();
}

}
