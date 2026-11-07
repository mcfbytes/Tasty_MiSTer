// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "app/dip_cell.h"
#include "app/link_tx_channel.h"
#include "app/page_description.h"
#include "app/ui_request_ring.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/page.h"

namespace mister::ui {

[[nodiscard]] std::string dip_row_text(const app::DipTable::Row& row);

class DipsPage final : public Page {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit DipsPage(const app::DipTable& table) noexcept : table_(&table) {}

    PageId id() const override {
        TASTY_SEAT_BODY(DipsPage);
        return PageId::ArcadeDip;
    }

    void on_enter(PageContext& ctx) override;

    bool walk(PageWalk& out) const override;
    Transition handle(const UiEvent& ev) override;

    MenuMask mask() const override {
        TASTY_SEAT_BODY(DipsPage);
        return dense_mask(reset_row().before() + kOneRow);
    }

    void set_selection(RowIndex i) override {
        TASTY_SEAT_BODY(DipsPage);
        sel_ = i;
        track_viewport();
    }

    RowIndex selection() const override {
        TASTY_SEAT_BODY(DipsPage);
        return sel_;
    }

    std::uint32_t refusals() const noexcept override {
        TASTY_SEAT_BODY(DipsPage);
        return refusals_;
    }

    std::uint32_t commands_published() const noexcept { return published_; }

private:
    RowIndex reset_row() const noexcept { return row_after(RowCount::of(table_->count)); }

    void save_dips();
    template <class A>
    void push_op(const A& op);
    void track_viewport() noexcept;

    const app::DipTable* table_;
    infra::OptRef<app::UiRequestRing> asks_{};
    infra::OptRef<app::LinkTxChannel<SeatTag::Ui>> link_tx_{};
    RowIndex sel_{};
    RowIndex first_row_{};
    std::uint32_t refusals_ = 0;
    std::uint32_t published_ = 0;
};

}
