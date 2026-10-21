// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/hd_about_plan.h"

#include <cstdint>
#include <string>
#include <string_view>

#include "ui/hd_plan_inputs.h"
#include "ui/page_context.h"

namespace mister::ui {
namespace {

constexpr std::string_view kVersionLabel = "Version";
constexpr std::string_view kLinuxLabel = "Linux";
constexpr std::string_view kKernelLabel = "Kernel";
constexpr std::string_view kStorageLabel = "Storage";

std::string storage_word(const SystemFacts& f) {
    std::string s = f.storage_usb ? "USB" : (f.storage_sd_fallback ? "SD (fallback)" : "SD");
    s += ", ";
    constexpr std::uint64_t kTenGiB = 10ull * 1024 * 1024 * 1024;
    if (f.avail_bytes < kTenGiB) {
        s += std::to_string(f.avail_bytes / (1024ull * 1024));
        s += " MB";
    } else {
        s += std::to_string(f.avail_bytes / (1024ull * 1024 * 1024));
        s += " GB";
    }
    return s;
}

bool same_facts(const SystemFacts& a, const SystemFacts& b) {
    return std::string_view(a.version) == std::string_view(b.version)
        && std::string_view(a.os_version) == std::string_view(b.os_version)
        && a.avail_bytes / (1024ull * 1024) == b.avail_bytes / (1024ull * 1024)
        && a.storage_usb == b.storage_usb
        && a.storage_sd_fallback == b.storage_sd_fallback
        && a.kernel == b.kernel;
}

}

HdAboutPlan::HdAboutPlan(const HdPlanInputs& in) : facts_(in.ctx.sys) {
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
    add(kVersionLabel, facts_.version);
    add(kLinuxLabel, facts_.os_version);
    add(kKernelLabel, kernel_word_());
    add(kStorageLabel, storage_word(facts_));
    for (HdValueRow& row : rows_)
        refs_.emplace_back(row);
}

std::string_view HdAboutPlan::kernel_word_() const noexcept {
    switch (facts_.kernel) {
        case KernelRt::PreemptRt:
            return "PREEMPT_RT";
        case KernelRt::NotRt:
            return "Not RT";
        case KernelRt::Unknown:
            break;
    }
    return "Unknown";
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

void HdAboutPlan::refresh(const SystemFacts& facts) {
    TASTY_SEAT_BODY(HdAboutPlan);
    if (same_facts(facts_, facts))
        return;
    facts_ = facts;
    build_();
}

}
