// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "app/recent_games.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/confirm_host.h"
#include "ui/options_model.h"
#include "ui/placer.h"
#include "ui/slot_pick_host.h"

namespace mister::ui {

class OptionItem;
struct HdPlanInputs;
struct PageContext;

class RecentLaunch final {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit RecentLaunch(const HdPlanInputs& in);
    RecentLaunch(const RecentLaunch&) = delete;
    RecentLaunch& operator=(const RecentLaunch&) = delete;

    [[nodiscard]] bool asks_first(const app::RecentGames::Record& r) const;

    Transition launch(const app::RecentGames::Record& r);

    [[nodiscard]] bool ask(
        IConfirmHost& host,
        std::string_view title,
        std::span<const std::string_view> body
    );

    [[nodiscard]] std::uint32_t refusals() const noexcept;
    [[nodiscard]] std::uint32_t commands_published() const noexcept;

private:
    [[nodiscard]] infra::OptRef<const OptionItem> slot_for_(
        const app::RecentGames::Record& r
    ) const;

    PageContext& ctx_;
    std::string running_core_;
    SlotPickHost::Env env_;
    OptionsModel walk_model_;
    std::optional<Placer> placer_;
    std::optional<SlotPickHost> picks_;
    bool front_end_ = false;
    std::uint32_t refusals_ = 0;
    std::uint32_t published_ = 0;
};

}
