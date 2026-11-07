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

proto::FieldValue StatusView::field(BitField b) const noexcept {
    if (!b.valid())
        return {};
    std::uint32_t out = 0;
    for (unsigned i = 0; i < b.width; ++i) {
        const unsigned bit = b.start.v + i;
        if (bit >= 128)
            break;
        if (status.get_bit(proto::StatusBit{static_cast<std::uint8_t>(bit)}))
            out |= (1u << i);
    }
    return proto::FieldValue{out};
}

proto::FieldValue cycle_value(
    BitField bits,
    proto::FieldValue current,
    CycleDir dir,
    const ISlotLabels* labels
) {
    if (!bits.valid())
        return current;
    const std::uint32_t m = bits.mask();
    const bool forward = dir == CycleDir::Next;

    std::uint32_t raw = (current.v + (forward ? 1u : ~0u)) & m;
    proto::FieldValue v{raw};

    if (forward) {
        if (labels != nullptr && !labels->has_label(v))
            v = proto::FieldValue{};
        return v;
    }

    const std::uint64_t slots = static_cast<std::uint64_t>(m) + 1u;
    for (std::uint64_t i = 0; i < slots; ++i) {
        if (labels == nullptr || labels->has_label(v))
            return v;
        raw = (raw - 1u) & m;
        v = proto::FieldValue{raw};
    }
    return current;
}

Ex<void> OptionsModel::build(const ItemTable& table, const BuildEnv& env) {
    items_.clear();
    selectable_ = RowCount{};
    drawn_ = RowCount{};
    beyond_mask_ = RowCount{};
    page_ = env.page;
    flat_ = env.flat;

    OptionItem::Carry carry{};

    const auto emit = [&](std::unique_ptr<OptionItem> it) {
        if (kind_draws(it->kind)) {
            it->screen_row = osd_row_at(drawn_);
            drawn_ += kOneRow;
        }
        if (kind_selects(it->kind)) {
            it->selectable_index = row_after(selectable_);
            selectable_ += kOneRow;
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
        bool inpage = (env.page == proto::kRootSubmenu);
        std::unique_ptr<OptionItem> it;

        if (src.kind == proto::ItemKind::Dip) {

            if (env.page == proto::kRootSubmenu && env.dip_count != 0) {
                emit(synthesise(OptionItem::Kind::Dip, false));
            }
            continue;
        }

        it = OptionItem::make(kind);
        if (src.kind != proto::ItemKind::DefMra) {

            if (src.declares_page) {
                if (env.flat && env.page == proto::kRootSubmenu && !carry.last_space) {
                    emit(synthesise(OptionItem::Kind::Separator, vis.disabled));
                }
            } else if (src.page != proto::kRootSubmenu) {
                if (env.page != proto::kRootSubmenu && env.page == src.page)
                    inpage = true;
                if (env.page == proto::kRootSubmenu && !env.flat)
                    inpage = false;

                if (env.flat && env.page == proto::kRootSubmenu && it->hidden_in_flat_root()) {
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

    if (!drawn_.empty()) {
        std::unique_ptr<OptionItem> it = synthesise(OptionItem::Kind::Exit, false);

        const RowCount last = env.osd_rows.empty() ? RowCount{} : env.osd_rows - kOneRow;
        drawn_ = std::max(drawn_, last);
        emit(std::move(it));
    }

    if (selectable_ > menu_mask_rows())
        beyond_mask_ = selectable_ - menu_mask_rows();
    return {};
}

MenuMask OptionsModel::mask() const noexcept {
    return dense_mask(selectable_);
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
    view_->on_model_rebuilt();
}

void CoreOptionsPage::rebuild(const ItemTable& table, const BuildEnv& env) {
    TASTY_SEAT_BODY(CoreOptionsPage);
    env_ = SlotPickHost::Env{env};
    (void)model_.build(table, view_->walk_env(env));
    view_->on_model_rebuilt();
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

void CoreOptionsPage::write_status(BitField bits, proto::FieldValue value) {
    TASTY_SEAT_BODY(CoreOptionsPage);
    push_op(proto::LinkOp::WriteStatus{.start = bits.start, .width = bits.width, .value = value.v});
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
