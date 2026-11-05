// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

#include "app/page_description.h"
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

    static constexpr RowCount kMaxRows{64};

private:
    struct Slots {
        RowCount body{};
        RowCount pinned{};
        RowCount dropped{};

        [[nodiscard]] RowCount addressed() const noexcept { return body + pinned; }

        [[nodiscard]] RowIndex end() const noexcept { return row_after(addressed()); }
    };

    [[nodiscard]] Slots slots_() const;
    [[nodiscard]] IHdRow& row_(const Slots& s, RowIndex slot) const;
    [[nodiscard]] bool shown_(const Slots& s, RowIndex slot) const;
    [[nodiscard]] bool focusable_(RowIndex slot) const;
    [[nodiscard]] std::optional<RowIndex> first_focusable_from_(RowIndex slot) const;
    [[nodiscard]] std::optional<RowIndex> recalled_() const;

    struct Window {
        std::array<RowIndex, app::PageDescription::kMaxRows> slot{};
        std::uint8_t count = 0;
        std::uint8_t pinned = 0;
        std::uint16_t first = 0;
        std::uint16_t total = 0;
    };

    [[nodiscard]] Window window_(std::uint16_t prev_first) const;
    void focus_(RowIndex i);

    void refocus_();

    std::unique_ptr<IHdPlan> plan_;
    ItemId key_;
    HdFocusMemory& memory_;
    RowIndex sel_{};
    std::uint32_t refusals_ = 0;
};

}
