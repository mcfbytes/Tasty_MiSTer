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
#include "ui/types.h"

namespace mister::ui {

class HdFocusMemory;

class HdListPage final : public Page, public IMaskRebuild {
    TASTY_SEAT_RESIDENT(Ui);

public:
    HdListPage(std::unique_ptr<IHdPlan> plan, ItemId key, HdFocusMemory& memory);

    PageId id() const override;

    bool walk(PageWalk& out) const override;

    bool describe_hub(app::PageDescription& out) const override;
    Transition handle(const UiEvent& ev) override;
    void on_facts(const SystemFacts& facts) override;

    infra::OptRef<IMaskRebuild> mask_rebuild() override;
    void rebuild(const proto::ItemTable& table, const BuildEnv& env) override;
    proto::SubmenuNo page() const noexcept override;

    MenuMask mask() const override;
    void set_selection(RowIndex i) override;
    RowIndex selection() const override;

    std::optional<TierRun> tier_run(RowIndex at) const override;

    [[nodiscard]] bool empty() const;
    std::uint32_t refusals() const noexcept override;

    [[nodiscard]] RowCount rows_dropped() const noexcept;

    static constexpr RowCount kMaxRows = menu_mask_rows();

private:
    struct Slots {
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

    [[nodiscard]] Slots slots_() const;
    [[nodiscard]] IHdRow& row_(const Slots& s, RowIndex slot) const;
    [[nodiscard]] bool shown_(const Slots& s, RowIndex slot) const;
    [[nodiscard]] bool focusable_(RowIndex slot) const;
    [[nodiscard]] std::optional<RowIndex> first_focusable_from_(RowIndex slot) const;
    [[nodiscard]] std::optional<RowIndex> recalled_() const;

    struct Window {
        infra::IndexedArray<RowIndex, app::PageDescription::kMaxRows, RowIndex> slot{};
        RowCount count{};
        RowCount pinned{};
        RowIndex first{};
        RowCount total{};
    };

    [[nodiscard]] Window window_(RowIndex prev_first) const;
    void focus_(RowIndex i);

    void refocus_();

    std::unique_ptr<IHdPlan> plan_;
    ItemId key_;
    HdFocusMemory& memory_;
    RowIndex sel_{};
    std::uint32_t refusals_ = 0;
};

}
