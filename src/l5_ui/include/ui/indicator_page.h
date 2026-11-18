// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>

#include "infra/seat.h"
#include "ui/indicator_badge.h"
#include "ui/indicator_board.h"
#include "ui/page.h"

namespace mister::ui {

class IndicatorPage final : public Page {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit IndicatorPage(const IndicatorBoard& board) noexcept : board_(board) {}

    PageId id() const override {
        TASTY_SEAT_BODY(IndicatorPage);
        return PageId::Indicators;
    }

    PageFacts facts() const override;

    MenuMask mask() const override {
        TASTY_SEAT_BODY(IndicatorPage);
        return MenuMask{};
    }

    bool walk(PageWalk&) const override {
        TASTY_SEAT_BODY(IndicatorPage);
        return false;
    }

    std::optional<proto::InfoWindow> info_window() const override;

    app::InfoAnchor info_anchor() const override {
        TASTY_SEAT_BODY(IndicatorPage);
        return board_.anchor();
    }

    void render(OsdRenderer& out) const override;

    Transition handle(const UiEvent&) override {
        TASTY_SEAT_BODY(IndicatorPage);
        return Transition{};
    }

private:
    [[nodiscard]] IndicatorBadge badge_() const noexcept;

    const IndicatorBoard& board_;
};

[[nodiscard]] std::unique_ptr<Page> make_indicator_page(const IndicatorBoard& board);

}
