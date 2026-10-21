// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/options_page.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "app/page_description.h"
#include "ui/core_options_page.h"
#include "ui/menu_model.h"
#include "ui/option_host.h"
#include "ui/options_model.h"
#include "ui/page_ask.h"
#include "ui/page_context.h"
#include "ui/slot_labels.h"
#include "ui/status_view.h"
#include "ui/ui_event.h"

namespace mister::ui {
namespace {

using proto::Item;
using proto::ItemTable;
using proto::OsdRow;

}

std::uint32_t StatusView::field(BitField b) const noexcept {
    if (!b.valid())
        return 0;
    std::uint32_t out = 0;
    for (unsigned i = 0; i < b.width; ++i) {
        const unsigned bit = b.start.v + i;
        if (bit >= 128)
            break;
        if (((w[bit / 16] >> (bit % 16)) & 1u) != 0u)
            out |= (1u << i);
    }
    return out;
}

std::uint32_t cycle_value(
    BitField bits,
    std::uint32_t current,
    int dir,
    const ISlotLabels* labels
) {
    if (!bits.valid())
        return current;
    const std::uint32_t m = bits.mask();
    const bool forward = dir >= 0;
    std::uint32_t v = forward ? ((current + 1u) & m) : ((current - 1u) & m);

    if (forward) {
        if (labels != nullptr && !labels->has_label(v))
            v = 0;
        return v;
    }

    const std::uint64_t slots = static_cast<std::uint64_t>(m) + 1u;
    for (std::uint64_t i = 0; i < slots; ++i) {
        if (labels == nullptr || labels->has_label(v))
            return v;
        v = (v - 1u) & m;
    }
    return current;
}

Ex<void> OptionsModel::build(const ItemTable& table, const BuildEnv& env) {
    items_.clear();
    selectable_ = RowCount{};
    drawn_ = RowCount{};
    beyond_mask_ = 0;
    page_ = env.page;
    flat_ = env.flat;

    OptionItem::Carry carry{};

    const auto emit = [&](std::unique_ptr<OptionItem> it) {
        if (kind_draws(it->kind)) {
            it->screen_row = OsdRow{static_cast<std::uint8_t>(drawn_.v)};
            drawn_.v += 1;
        }
        if (kind_selects(it->kind)) {
            it->selectable_index = RowIndex{selectable_.v};
            selectable_.v += 1;
        }
        items_.push_back(std::move(it));
    };

    const auto synthesise = [&](OptionItem::Kind k, bool disabled) {
        std::unique_ptr<OptionItem> it = OptionItem::make(k);
        it->disabled = disabled;
        (void)it->build(OptionItem::Source{nullptr, {}, selectable_, carry}, env);
        return it;
    };

    const auto all = table.items();
    for (const Item& src : all) {
        const OptionItem::Kind kind = kind_of(src.kind);
        const proto::Visibility vis = proto::evaluate(src, env.hdmask);
        bool inpage = (env.page == 0);
        std::unique_ptr<OptionItem> it;

        if (src.kind == proto::ItemKind::Dip) {

            if (env.page == 0 && env.dip_count != 0) {
                emit(synthesise(OptionItem::Kind::Dip, false));
            }
            continue;
        }

        it = OptionItem::make(kind);
        if (src.kind != proto::ItemKind::DefMra) {

            if (src.declares_page) {
                if (env.flat && env.page == 0 && !carry.last_space) {
                    emit(synthesise(OptionItem::Kind::Separator, vis.disabled));
                }
            } else if (src.page != 0) {
                if (env.page != 0 && env.page == src.page)
                    inpage = true;
                if (env.page == 0 && !env.flat)
                    inpage = false;

                if (env.flat && env.page == 0 && it->hidden_in_flat_root()) {
                    inpage = false;
                }
            }
        }

        carry.last_space = false;
        if (vis.hidden || !inpage)
            continue;

        if (it->leads_with_manual_row() && env.manual_available) {
            emit(synthesise(OptionItem::Kind::Manual, false));
        }

        const proto::ItemOrdinal ordinal{static_cast<std::uint16_t>(&src - all.data())};
        it->item = ordinal;
        it->disabled = vis.disabled;
        it->ext_word = (src.ex == proto::ExShift::Plus32);
        it->page = src.page;
        it->bits = src.bits;
        carry = it->build(OptionItem::Source{&table, ordinal, selectable_, carry}, env);
        emit(std::move(it));
    }

    if (drawn_.v != 0) {
        std::unique_ptr<OptionItem> it = synthesise(OptionItem::Kind::Exit, false);

        const std::uint32_t last = (env.osd_rows == 0)
            ? 0u
            : static_cast<std::uint32_t>(env.osd_rows - 1);
        drawn_.v = std::max(drawn_.v, last);
        emit(std::move(it));
    }

    if (selectable_.v > 64)
        beyond_mask_ = selectable_.v - 64;
    return {};
}

MenuMask OptionsModel::mask() const noexcept {
    const std::uint32_t n = selectable_.v;
    if (n >= 64)
        return MenuMask{~0ull};
    return MenuMask{(1ull << n) - 1ull};
}

const OptionItem* OptionsModel::at_selectable(RowIndex index) const {
    for (const std::unique_ptr<OptionItem>& it : items_) {
        if (it->selectable_index == index)
            return it.get();
    }
    return nullptr;
}

CoreOptionsPage::CoreOptionsPage(
    const ItemTable& table,
    const BuildEnv& env,
    std::string_view title,
    infra::OptRef<const StatusView> status
)
    : title_(title),
      env_(env),
      status_(status),
      view_(std::make_unique<ClassicOptionsView>(static_cast<IOptionsSurface&>(*this))) {
    first_walk_(table, env);
}

CoreOptionsPage::CoreOptionsPage(
    const ItemTable& table,
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

void CoreOptionsPage::first_walk_(const ItemTable& table, const BuildEnv& env) {
    TASTY_SEAT_BODY(CoreOptionsPage);
    (void)model_.build(table, view_->walk_env(env));
    view_->on_walk();
}

void CoreOptionsPage::rebuild(const ItemTable& table, const BuildEnv& env) {
    TASTY_SEAT_BODY(CoreOptionsPage);
    env_ = SlotPickHost::Env{env};
    (void)model_.build(table, view_->walk_env(env));
    view_->on_walk();
}

void CoreOptionsPage::on_enter(PageContext& ctx) {
    TASTY_SEAT_BODY(CoreOptionsPage);
    asks_ = ctx.ui_requests;
    link_tx_ = ctx.link_tx;

    if (picks_) {
        refusals_ += picks_->refusals();
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

void CoreOptionsPage::render(OsdRenderer& out) const {
    TASTY_SEAT_BODY(CoreOptionsPage);
    view_->render(out);
}

bool CoreOptionsPage::describe(app::PageDescription& out) const {
    TASTY_SEAT_BODY(CoreOptionsPage);
    return view_->describe(out);
}

Transition CoreOptionsPage::handle(const UiEvent& ev) {
    TASTY_SEAT_BODY(CoreOptionsPage);
    return view_->handle(ev);
}

const OptionsModel& CoreOptionsPage::walk() const {
    TASTY_SEAT_BODY(CoreOptionsPage);
    return model_;
}

std::string_view CoreOptionsPage::heading() const {
    TASTY_SEAT_BODY(CoreOptionsPage);
    return title_;
}

std::uint32_t CoreOptionsPage::field(BitField bits) const {
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
        ++refusals_;
        return Transition{};
    }
    return picks_->open_recents(it);
}

void CoreOptionsPage::save_config() {
    TASTY_SEAT_BODY(CoreOptionsPage);
    if (!push_ask(asks_, app::UiRequest::SaveConfig{})) {
        ++refusals_;
        return;
    }
    ++published_;
}

void CoreOptionsPage::refuse() {
    TASTY_SEAT_BODY(CoreOptionsPage);
    ++refusals_;
}

void CoreOptionsPage::write_status(BitField bits, std::uint32_t value) {
    TASTY_SEAT_BODY(CoreOptionsPage);
    push_op(proto::LinkOp::WriteStatus{.start = bits.start, .width = bits.width, .value = value});
}

void CoreOptionsPage::toggle(BitField bits, bool ex) {
    TASTY_SEAT_BODY(CoreOptionsPage);
    const app::UiRequest::ResetCore ask{
        .edge = proto::ResetEdge::osd_toggle(bits.start, ex),
        .scope = app::CoreScope{env_.conf_gen}
    };
    if (!push_ask(asks_, ask)) {
        ++refusals_;
        return;
    }
    ++published_;
}

template <class A>
void CoreOptionsPage::push_op(const A& op) {
    if (!link_tx_ || !link_tx_->push(op)) {
        ++refusals_;
        return;
    }
    ++published_;
}

Transition CoreOptionsPage::open_browser(const OptionItem& it) {
    TASTY_SEAT_BODY(CoreOptionsPage);
    if (!picks_) {
        ++refusals_;
        return Transition{};
    }
    return picks_->open(it);
}

}
