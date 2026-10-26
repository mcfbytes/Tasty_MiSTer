// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "app/page_description.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/hd_plan.h"
#include "ui/hd_rewalk.h"
#include "ui/hd_page0_walk.h"
#include "ui/slot_focus.h"

namespace mister::ui {

class NowPlaying;
class OptionItem;
struct CoreVerbs;
struct HdPlanInputs;

class HdMediaPlan final : public IHdPlan, public IHdRewalk {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit HdMediaPlan(const HdPlanInputs& in);
    HdMediaPlan(const HdMediaPlan&) = delete;
    HdMediaPlan& operator=(const HdMediaPlan&) = delete;

    PageId id() const override;
    std::string_view title() const override;
    std::span<const std::reference_wrapper<IHdRow>> rows() const override;
    infra::OptRef<IHdRewalk> rewalker() override;
    void rewalk(const proto::ItemTable& table, const BuildEnv& env) override;

private:
    class SlotRow final : public IHdRow {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        SlotRow(HdMediaPlan& plan, std::size_t at) noexcept : plan_(plan), at_(at) {}

        std::optional<RowKey> key() const override;
        app::PageDescription::Row describe() const override;
        Transition select() override;

    private:
        HdMediaPlan& plan_;
        std::size_t at_;
    };

    void walk_(const proto::ItemTable& table, const BuildEnv& env);

    [[nodiscard]] const OptionItem& slot_(std::size_t at) const {
        return *page0_.placer().slots()[at];
    }

    [[nodiscard]] SlotFocus focus_at_(std::size_t at) const;

    const NowPlaying& now_playing_;
    const CoreVerbs& verbs_;
    const bool front_end_;
    HdPage0Walk page0_;
    std::vector<SlotRow> rows_;
    std::vector<std::reference_wrapper<IHdRow>> refs_;
};

}
