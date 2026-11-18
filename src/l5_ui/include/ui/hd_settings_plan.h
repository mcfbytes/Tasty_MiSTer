// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "app/page_description.h"
#include "infra/seat.h"
#include "ui/hd_plan.h"
#include "ui/hd_row.h"
#include "ui/hd_rows/hd_action.h"
#include "ui/hd_rows/hd_ask_row.h"
#include "ui/hd_rows/hd_push_row.h"
#include "ui/hd_rows/hd_separator_row.h"
#include "ui/page_id.h"
#include "ui/scripts_entry.h"
#include "ui/transition.h"
#include "svc/types.h"

namespace mister::ui {

struct HdPlanInputs;
struct PageContext;

class HdSettingsPlan final : public IHdPlan {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit HdSettingsPlan(const HdPlanInputs& in);
    HdSettingsPlan(const HdSettingsPlan&) = delete;
    HdSettingsPlan& operator=(const HdSettingsPlan&) = delete;

    PageId id() const override;
    std::string_view title() const override;
    std::span<const std::reference_wrapper<IHdRow>> rows() const override;
    void refresh(const SystemFacts& facts, const SessionFacts& session) override;
    RowCount pinned() const noexcept override;
    svc::Tally refusals() const noexcept override;
    [[nodiscard]] std::uint32_t commands_published() const noexcept;

private:
    class HdEnterClassicRow final : public IHdRow {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        explicit HdEnterClassicRow(PageId classic) noexcept : classic_(classic) {}

        std::optional<RowKey> key() const override;
        app::PageDescription::Row describe() const override;
        Transition select() override;

    private:
        PageId classic_;
    };

    class ScriptsRow final : public IHdRow {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        explicit ScriptsRow(HdSettingsPlan& plan) noexcept : plan_(plan) {}

        app::PageDescription::Row describe() const override;
        Transition select() override;

    private:
        HdSettingsPlan& plan_;
    };

    class AutosaveRow final : public IHdRow {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        explicit AutosaveRow(HdSettingsPlan& plan) noexcept : plan_(plan) {}

        app::PageDescription::Row describe() const override;
        Transition select() override;

    private:
        HdSettingsPlan& plan_;
    };

    class RebootAction final : public IHdAction {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        explicit RebootAction(HdSettingsPlan& plan) noexcept : plan_(plan) {}

        Transition on_yes() override;

    private:
        HdSettingsPlan& plan_;
    };

    PageContext& ctx_;
    ScriptsEntry scripts_entry_;
    svc::Tally refusals_{};
    bool autosave_on_ = false;
    std::uint32_t published_ = 0;
    RebootAction reboot_action_{*this};
    HdPushRow controllers_;
    HdPushRow display_;
    HdPushRow network_;
    AutosaveRow autosave_{*this};
    ScriptsRow scripts_{*this};
    HdPushRow about_;
    HdAskRow reboot_;
    HdSeparatorRow separator_;
    HdEnterClassicRow advanced_;
    std::vector<std::reference_wrapper<IHdRow>> refs_;
};

}
