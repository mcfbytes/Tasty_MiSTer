// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "app/page_description.h"
#include "infra/indexed_array.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/hd_plan.h"
#include "ui/page.h"
#include "ui/page_arg.h"
#include "ui/types.h"
#include "svc/types.h"

namespace mister::ui {

class HdFocusMemory;

class HdListPage final : public Page, public IMaskRebuild {
    TASTY_SEAT_RESIDENT(Ui);

public:
    HdListPage(std::unique_ptr<IHdPlan> plan, PageArg push_arg, HdFocusMemory& memory);

    PageId id() const override;

    PageFacts facts() const override;

    bool walk(PageWalk& out) const override;

    bool describe_hub(app::PageDescription& out) const override;
    Transition handle(const UiEvent& ev) override;
    void on_facts(const SystemFacts& facts, const SessionFacts& session) override;

    infra::OptRef<IMaskRebuild> mask_rebuild() override;
    void rebuild(const proto::ItemTable& table, const BuildEnv& env) override;
    proto::SubmenuNo page() const noexcept override;

    MenuMask mask() const override;
    void set_selection(RowIndex i) override;
    RowIndex selection() const override;

    std::optional<TierRun> tier_run(RowIndex at) const override;

    [[nodiscard]] bool empty() const;
    svc::Tally refusals() const noexcept override;
    ConnectDemand connect_demand() const override;
    bool holds_live_script() const override;
    bool bt_pairing_active() const override;

    [[nodiscard]] RowCount rows_dropped() const noexcept;

    static constexpr RowCount kMaxRows = menu_mask_rows();

private:
    struct PageRows {
        RowCount body{};
        RowCount pinned{};
        RowCount dropped{};

        [[nodiscard]] RowCount addressed() const noexcept { return body + pinned; }

        [[nodiscard]] RowIndex end() const noexcept { return row_after(addressed()); }
    };

    template <std::size_t Extent>
    struct RowSet {
        infra::IndexedArray<RowIndex, Extent, RowIndex> at{};
        RowCount n{};

        void add(RowIndex r) noexcept {
            if (n < RowCount::of(Extent))
                at[row_after(n)] = r;
            n += kOneRow;
        }

        [[nodiscard]] RowIndex operator[](RowIndex i) const noexcept { return at[i]; }
    };

    [[nodiscard]] PageRows page_rows_() const;
    [[nodiscard]] IHdRow& row_(const PageRows& s, RowIndex page_row) const;
    [[nodiscard]] bool shown_(const PageRows& s, RowIndex page_row) const;
    [[nodiscard]] bool focusable_(RowIndex page_row) const;
    [[nodiscard]] std::optional<RowIndex> first_focusable_from_(RowIndex page_row) const;
    [[nodiscard]] std::optional<RowIndex> recalled_() const;

    struct Window {
        infra::IndexedArray<RowIndex, app::PageDescription::kMaxRows, RowIndex> page_row{};
        RowCount count{};
        RowCount pinned{};
        RowIndex first{};
        RowCount total{};
    };

    [[nodiscard]] Window window_(RowIndex prev_first) const;
    void focus_(RowIndex i);

    void refocus_();

    std::unique_ptr<IHdPlan> plan_;
    PageArg push_arg_;
    HdFocusMemory& memory_;
    RowIndex sel_{};
    svc::Tally refusals_{};
};

}
