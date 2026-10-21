// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "app/page_description.h"
#include "app/recent_games.h"
#include "infra/seat.h"
#include "ui/confirm_host.h"
#include "ui/hd_row.h"
#include "ui/recent_launch.h"

namespace mister::ui {

class RecentRow final : public IHdRow, public IConfirmHost {
    TASTY_SEAT_RESIDENT(Ui);

public:
    RecentRow(RecentLaunch& launch, app::RecentGames::Record record);

    bool shown() const override;
    std::optional<RowKey> key() const override;
    app::PageDescription::Row describe() const override;
    Transition select() override;
    Transition on_confirm(std::uint8_t choice) override;

    [[nodiscard]] const app::RecentGames::Record& record() const noexcept { return record_; }

    static constexpr std::uint32_t kKeySpaceMask = 0xC000'0000u;
    static constexpr std::uint32_t kKeySpace = 0x4000'0000u;

private:
    RecentLaunch& launch_;
    app::RecentGames::Record record_;
    std::string title_;

    std::array<std::string, 2> ask_text_{};
    std::array<std::string_view, 2> ask_body_{};
};

}
