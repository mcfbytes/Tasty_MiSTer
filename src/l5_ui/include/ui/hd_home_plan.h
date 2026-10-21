// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "app/page_description.h"
#include "infra/seat.h"
#include "ui/hd_plan.h"
#include "ui/recent_row.h"
#include "ui/hd_rows/hd_gated_row.h"
#include "ui/hd_rows/hd_pair_row.h"
#include "ui/hd_rows/hd_push_row.h"
#include "ui/hd_rows/hd_separator_row.h"
#include "ui/page.h"

namespace mister::ui {

struct HdPlanInputs;
struct PageContext;

class HdHomePlan final : public IHdPlan {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit HdHomePlan(const HdPlanInputs& in);
    HdHomePlan(const HdHomePlan&) = delete;
    HdHomePlan& operator=(const HdHomePlan&) = delete;

    PageId id() const override;
    std::string_view title() const override;
    std::span<const std::reference_wrapper<IHdRow>> rows() const override;
    std::uint8_t pinned() const noexcept override;
    bool remembers_focus() const noexcept override;
    std::uint32_t refusals() const noexcept override;

private:
    class BrowseRow final : public IHdRow {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        explicit BrowseRow(HdHomePlan& plan) noexcept : plan_(plan) {}

        bool shown() const override;
        app::PageDescription::Row describe() const override;
        Transition select() override;

    private:
        HdHomePlan& plan_;
    };

    PageContext& ctx_;
    std::unique_ptr<Page> cores_;
    std::uint32_t refusals_ = 0;

    RecentLaunch launch_;
    std::vector<RecentRow> played_;
    bool has_any_ = false;
    BrowseRow browse_{*this};
    HdPairRow pair_;
    HdPushRow all_row_;
    HdGatedRow all_gate_{all_row_, has_any_};
    HdSeparatorRow separator_;
    HdPushRow settings_;
    std::vector<std::reference_wrapper<IHdRow>> refs_;
};

}
