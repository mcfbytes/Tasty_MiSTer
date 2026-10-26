// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "app/page_description.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/hd_plan.h"
#include "ui/hd_rewalk.h"
#include "ui/hd_page0_walk.h"
#include "ui/options_model.h"
#include "ui/placer.h"
#include "ui/slot_focus.h"
#include "ui/slot_pick_host.h"
#include "ui/types.h"

namespace mister::ui {

class NowPlaying;
class OptionItem;
struct CoreVerbs;
struct HdPlanInputs;
struct PageContext;

class HdSlotSheetPlan final : public IHdPlan, public IHdRewalk {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit HdSlotSheetPlan(const HdPlanInputs& in);
    HdSlotSheetPlan(const HdSlotSheetPlan&) = delete;
    HdSlotSheetPlan& operator=(const HdSlotSheetPlan&) = delete;

    PageId id() const override;
    std::string_view title() const override;
    std::span<const std::reference_wrapper<IHdRow>> rows() const override;
    infra::OptRef<IHdRewalk> rewalker() override;
    void rewalk(const proto::ItemTable& table, const BuildEnv& env) override;
    std::uint32_t refusals() const noexcept override;
    [[nodiscard]] std::uint32_t commands_published() const noexcept;

private:
    class ChooseRow final : public IHdRow {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        explicit ChooseRow(HdSlotSheetPlan& plan) noexcept : plan_(plan) {}

        bool shown() const override;
        app::PageDescription::Row describe() const override;
        Transition select() override;

    private:
        HdSlotSheetPlan& plan_;
    };

    class RecentRow final : public IHdRow {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        explicit RecentRow(HdSlotSheetPlan& plan) noexcept : plan_(plan) {}

        bool shown() const override;
        app::PageDescription::Row describe() const override;
        Transition select() override;

    private:
        HdSlotSheetPlan& plan_;
    };

    class EjectRow final : public IHdRow {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        explicit EjectRow(HdSlotSheetPlan& plan) noexcept : plan_(plan) {}

        bool shown() const override;
        app::PageDescription::Row describe() const override;
        Transition select() override;

    private:
        HdSlotSheetPlan& plan_;
    };

    void walk_(const proto::ItemTable& table, const BuildEnv& env);

    [[nodiscard]] bool has_slot_() const noexcept {
        return slot_.has_value() && picks_.has_value();
    }

    [[nodiscard]] bool ejectable_() const;

    PageContext& ctx_;
    const NowPlaying& now_playing_;
    const CoreVerbs& verbs_;
    const bool front_end_;
    const std::optional<SlotFocus> focus_;
    SlotPickHost::Env env_;
    HdPage0Walk page0_;
    std::optional<SlotPickHost> picks_;
    infra::OptRef<const OptionItem> slot_{};
    std::string title_;
    bool has_recent_ = false;
    std::uint32_t refusals_ = 0;

    ChooseRow choose_{*this};
    RecentRow recent_{*this};
    EjectRow eject_{*this};
    std::array<std::reference_wrapper<IHdRow>, 3> refs_{choose_, recent_, eject_};
};

}
