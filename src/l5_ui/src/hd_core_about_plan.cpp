// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_core_about_plan.h"

#include <array>
#include <cstddef>

#include "proto/conf_str.h"
#include "ui/hd_list_page.h"
#include "ui/hd_plan_inputs.h"

namespace mister::ui {
namespace {

struct AboutKind {
    proto::ItemKind kind;
    std::string_view label;
};

constexpr std::array<AboutKind, 3> kAboutKinds{{
    {proto::ItemKind::Version, "Version"},
    {proto::ItemKind::Info, {}},
    {proto::ItemKind::CfgVer, "Config version"},
}};

const AboutKind* about_kind(proto::ItemKind k) noexcept {
    for (const AboutKind& a : kAboutKinds) {
        if (a.kind == k)
            return &a;
    }
    return nullptr;
}

}

HdCoreAboutPlan::HdCoreAboutPlan(const HdPlanInputs& in) {
    TASTY_SEAT_BODY(HdCoreAboutPlan);
    for (const proto::Item& it : in.items.items()) {
        const AboutKind* a = about_kind(it.kind);
        if (a == nullptr || proto::evaluate(it, in.env.hdmask).hidden)
            continue;
        if (!a->label.empty()) {
            Line& l = lines_.emplace_back(Line{std::string(a->label), {}});
            (void)l.value.assign(in.items.subfield(it, 1));
            continue;
        }
        for (unsigned n = 1; n < it.subfield_count; ++n) {
            const std::string_view text = in.items.subfield(it, n);
            if (!text.empty())
                lines_.push_back(Line{std::string(text), {}});
        }
    }
    if (lines_.size() > HdListPage::kMaxRows)
        lines_.resize(HdListPage::kMaxRows);

    rows_.reserve(lines_.size());
    for (const Line& l : lines_) {
        refs_.emplace_back(
            rows_.emplace_back(HdValueRow::Spec{.label = l.label, .value = l.value})
        );
    }
}

PageId HdCoreAboutPlan::id() const {
    TASTY_SEAT_BODY(HdCoreAboutPlan);
    return PageId::HdCoreAbout;
}

std::string_view HdCoreAboutPlan::title() const {
    TASTY_SEAT_BODY(HdCoreAboutPlan);
    return "About this core";
}

std::span<const std::reference_wrapper<IHdRow>> HdCoreAboutPlan::rows() const {
    TASTY_SEAT_BODY(HdCoreAboutPlan);
    return refs_;
}

}
