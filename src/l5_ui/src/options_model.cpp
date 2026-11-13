// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/options_model.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace mister::ui {

void OptionsModel::build(const proto::ItemTable& table, const BuildEnv& env) {
    items_.clear();
    selectable_ = RowCount{};
    drawn_ = RowCount{};
    beyond_mask_ = RowCount{};
    page_ = env.page;
    flat_ = env.flat;

    OptionItem::Carry carry{};

    const auto emit = [&](std::unique_ptr<OptionItem> it) {
        if (kind_draws(it->kind)) {
            it->drawn_row = row_after(drawn_);
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
        it->page = env.page;
        (void)it->build(OptionItem::Source{nullptr, {}, selectable_, carry}, env);
        return it;
    };

    const auto all = table.items();
    for (const proto::Item& src : all) {
        const OptionItem::Kind kind = kind_of(src.kind);
        const proto::Visibility vis = proto::evaluate(src, env.hdmask);
        bool inpage = (env.page == proto::kRootSubmenu);
        std::unique_ptr<OptionItem> it;

        if (src.kind == proto::ItemKind::Dip) {

            if (env.page == proto::kRootSubmenu && !env.dip_count.empty()) {
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
}

MenuMask OptionsModel::mask() const noexcept {
    return dense_mask(selectable_);
}

infra::OptRef<const OptionItem> OptionsModel::at_selectable(RowIndex index) const {
    for (const std::unique_ptr<OptionItem>& it : items_) {
        if (it->selectable_index == index)
            return infra::OptRef<const OptionItem>{*it};
    }
    return {};
}

}
