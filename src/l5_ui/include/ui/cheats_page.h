// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "app/cheat_row.h"
#include "app/link_tx_channel.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/list_view.h"
#include "ui/page.h"

namespace mister::app {
class CheatStore;
}

namespace mister::ui {

[[nodiscard]] std::string cheat_row_text(const app::CheatRow& row);

[[nodiscard]] std::string cheats_title_text(std::size_t loaded);

class CheatsPage final : public Page {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit CheatsPage(app::CheatStore& store) noexcept;

    PageId id() const override {
        TASTY_SEAT_BODY(CheatsPage);
        return PageId::Cheats;
    }

    void on_enter(PageContext& ctx) override;
    void render(OsdRenderer& out) const override;
    bool describe(app::PageDescription& out) const override;
    Transition handle(const UiEvent& ev) override;
    Transition on_tick(PageContext& ctx) override;

    MenuMask mask() const override {
        TASTY_SEAT_BODY(CheatsPage);
        return MenuMask{};
    }

    RowIndex selection() const override {
        TASTY_SEAT_BODY(CheatsPage);
        return view_.viewport().selected;
    }

    std::uint32_t refusals() const noexcept override {
        TASTY_SEAT_BODY(CheatsPage);
        return refusals_;
    }

    std::uint32_t commands_published() const noexcept { return published_; }

private:
    void refresh_();
    void publish_apply_();

    app::CheatStore* store_;
    infra::OptRef<app::LinkTxChannel<SeatTag::Ui>> link_tx_{};
    std::vector<app::CheatRow> rows_;
    ListView<app::CheatRow> view_;
    std::uint32_t opens_seen_ = 0;
    std::uint32_t refusals_ = 0;
    std::uint32_t published_ = 0;
};

}
