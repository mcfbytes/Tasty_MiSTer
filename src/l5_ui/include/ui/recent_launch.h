// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "app/recents/recent_games.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "ui/confirm_host.h"
#include "ui/hd_page0_walk.h"
#include "ui/slot_pick_host.h"
#include "svc/types.h"

namespace mister::ui {

class OptionItem;
struct HdPlanInputs;
struct PageContext;

class RecentLaunch final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    enum class YesNo : std::uint8_t { Yes, No };

    [[nodiscard]] static constexpr RowIndex row_of(YesNo answer) noexcept {
        switch (answer) {
            case YesNo::Yes:
                return RowIndex{0};
            case YesNo::No:
                return RowIndex{1};
        }
        return RowIndex{};
    }

    explicit RecentLaunch(const HdPlanInputs& in);
    RecentLaunch(const RecentLaunch&) = delete;
    RecentLaunch& operator=(const RecentLaunch&) = delete;

    [[nodiscard]] bool asks_first(const app::recents::RecentGames::Record& r) const;

    Transition launch(const app::recents::RecentGames::Record& r);

    [[nodiscard]] Transition ask(
        IConfirmHost& host,
        std::string_view title,
        std::span<const std::string_view> body
    );

    [[nodiscard]] infra::Tally refusals() const noexcept;
    [[nodiscard]] std::uint32_t commands_published() const noexcept;

private:
    [[nodiscard]] infra::OptRef<const OptionItem> slot_for_(
        const app::recents::RecentGames::Record& r
    ) const;

    PageContext& ctx_;
    std::string running_core_;
    SlotPickHost::Env env_;
    HdPage0Walk page0_;
    std::optional<SlotPickHost> picks_;
    bool front_end_ = false;
    infra::Tally refusals_{};
    std::uint32_t published_ = 0;
};

}
