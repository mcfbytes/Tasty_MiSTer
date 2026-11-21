// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "app/hd/page_description.h"
#include "app/recents/recent_games.h"
#include "infra/seat.h"
#include "ui/confirm_host.h"
#include "ui/row.h"
#include "ui/recent_launch.h"

namespace mister::ui {

class RecentRow final : public IRow, public IConfirmHost {
    TASTY_SEAT_RESIDENT(Ui);

public:
    RecentRow(
        RecentLaunch& launch,
        app::recents::RecentGames::Record record,
        app::hd::RowTier tier = app::hd::RowTier::Place
    );

    bool shown() const override;
    std::optional<RowKey> key() const override;
    app::hd::PageDescription::Row describe(const RowCx&) const override;
    Transition select(const RowCx&) override;
    Transition on_confirm(RowIndex choice) override;

    [[nodiscard]] const app::recents::RecentGames::Record& record() const noexcept {
        return record_;
    }

private:
    RecentLaunch& launch_;
    app::recents::RecentGames::Record record_;
    app::hd::RowTier tier_;
    std::string title_;

    std::array<std::string, 2> ask_text_{};
    std::array<std::string_view, 2> ask_body_{};
};

}
