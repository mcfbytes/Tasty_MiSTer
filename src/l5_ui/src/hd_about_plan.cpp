// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_about_plan.h"

#include <string>
#include <string_view>
#include <utility>

#include "ui/free_space.h"
#include "ui/hd_plan_inputs.h"
#include "ui/page_context.h"

namespace mister::ui {
namespace {

constexpr std::string_view kVersionLabel = "Version";
constexpr std::string_view kLinuxLabel = "Linux";
constexpr std::string_view kKernelLabel = "Kernel";
constexpr std::string_view kStorageLabel = "Storage";

std::string_view kernel_word(KernelRt kernel) noexcept {
    switch (kernel) {
        case KernelRt::PreemptRt:
            return "PREEMPT_RT";
        case KernelRt::NotRt:
            return "Not RT";
        case KernelRt::Unknown:
            break;
    }
    return "Unknown";
}

std::string storage_word(const SystemFacts& f) {
    std::string s = f.storage_usb ? "USB" : (f.storage_sd_fallback ? "SD (fallback)" : "SD");
    s += ", ";
    s += format_free_space(f.avail, FreeSpaceStyle::Hd);
    return s;
}

}

auto HdAboutPlan::about_of(const SystemFacts& facts) -> AboutFacts {
    return AboutFacts{
        .version = std::string{facts.version.view()},
        .os_version = std::string{facts.os_version.view()},
        .kernel = std::string{kernel_word(facts.kernel)},
        .storage = storage_word(facts)
    };
}

HdAboutPlan::HdAboutPlan(const HdPlanInputs& in) : shown_(about_of(in.ctx.sys)) {
    TASTY_SEAT_BODY(HdAboutPlan);
    build_();
}

void HdAboutPlan::build_() {
    rows_.clear();
    refs_.clear();
    auto add = [this](std::string_view label, std::string_view text) {
        HdValueRow::Value v{};
        (void)v.assign(text);
        rows_.emplace_back(HdValueRow::Spec{.label = label, .value = v});
    };
    add(kVersionLabel, shown_.version);
    add(kLinuxLabel, shown_.os_version);
    add(kKernelLabel, shown_.kernel);
    add(kStorageLabel, shown_.storage);
    for (HdValueRow& row : rows_)
        refs_.emplace_back(row);
}

PageId HdAboutPlan::id() const {
    TASTY_SEAT_BODY(HdAboutPlan);
    return PageId::HdAbout;
}

std::string_view HdAboutPlan::title() const {
    TASTY_SEAT_BODY(HdAboutPlan);
    return "About";
}

std::span<const std::reference_wrapper<IHdRow>> HdAboutPlan::rows() const {
    TASTY_SEAT_BODY(HdAboutPlan);
    return refs_;
}

void HdAboutPlan::refresh(const SystemFacts& facts, const SessionFacts& session) {
    TASTY_SEAT_BODY(HdAboutPlan);
    (void)session;
    AboutFacts next = about_of(facts);
    if (next == shown_)
        return;
    shown_ = std::move(next);
    build_();
}

}
