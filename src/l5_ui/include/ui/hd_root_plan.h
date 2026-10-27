// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/page_description.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/hd_plan.h"
#include "ui/hd_rewalk.h"
#include "ui/hd_rows/hd_action.h"
#include "ui/hd_rows/hd_ask_row.h"
#include "ui/hd_rows/hd_close_row.h"
#include "ui/hd_rows/hd_gated_row.h"
#include "ui/hd_rows/hd_push_row.h"
#include "ui/hd_rows/hd_separator_row.h"
#include "ui/hd_page0_walk.h"
#include "ui/options_model.h"
#include "ui/placer.h"
#include "ui/slot_pick_host.h"

namespace mister::app {
class RecentGames;
}

namespace mister::ui {

class NowPlaying;
class OptionItem;
struct CoreVerbs;
struct HdPlanInputs;
struct PageContext;

class HdRootPlan final : public IHdPlan, public IHdRewalk {
    TASTY_SEAT_RESIDENT(Ui);

public:
    explicit HdRootPlan(const HdPlanInputs& in);
    HdRootPlan(const HdRootPlan&) = delete;
    HdRootPlan& operator=(const HdRootPlan&) = delete;

    PageId id() const override;
    std::string_view title() const override;
    std::span<const std::reference_wrapper<IHdRow>> rows() const override;
    std::uint8_t pinned() const noexcept override;
    bool remembers_focus() const noexcept override;
    bool hub() const noexcept override;
    void refresh(const SystemFacts& facts) override;
    infra::OptRef<IHdRewalk> rewalker() override;
    void rewalk(const proto::ItemTable& table, const BuildEnv& env) override;
    std::uint32_t refusals() const noexcept override;
    [[nodiscard]] std::uint32_t commands_published() const noexcept;

    static constexpr std::size_t kRows = 10;
    static constexpr std::size_t kReserved = 2;
    static_assert(kRows + kReserved <= app::PageDescription::kMaxRows);

    static constexpr std::uint8_t kPinned = 3;

    static constexpr app::RowTier kTier = app::RowTier::Verb;

private:
    class LoadRow final : public IHdRow {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        explicit LoadRow(HdRootPlan& plan) noexcept : plan_(plan) {}

        bool shown() const override;
        app::PageDescription::Row describe() const override;
        Transition select() override;

    private:
        HdRootPlan& plan_;
    };

    class ResetAction final : public IHdAction {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        explicit ResetAction(HdRootPlan& plan) noexcept : plan_(plan) {}

        Transition on_yes() override;

    private:
        HdRootPlan& plan_;
    };

    class MainMenuAction final : public IHdAction {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        explicit MainMenuAction(HdRootPlan& plan) noexcept : plan_(plan) {}

        Transition on_yes() override;

    private:
        HdRootPlan& plan_;
    };

    void walk_(const proto::ItemTable& table, const BuildEnv& env);

    void gate_() noexcept;
    Transition reset_yes_();
    Transition main_menu_yes_();

    PageContext& ctx_;
    const NowPlaying& now_playing_;
    const app::RecentGames& recent_games_;
    const CoreVerbs& verbs_;
    const bool front_end_;
    const bool table_settings_;
    SlotPickHost::Env env_;
    HdPage0Walk page0_;
    std::optional<SlotPickHost> picks_;
    infra::OptRef<const OptionItem> primary_{};
    std::string load_label_;

    bool has_reset_ = false;
    bool has_cheats_ = false;
    bool has_settings_ = false;

    bool has_media_ = false;

    bool has_recent_ = false;
    bool resume_ = false;
    bool cheats_ = false;
    bool reset_ = false;
    bool core_settings_ = false;
    bool main_menu_ = false;
    std::uint32_t refusals_ = 0;
    std::uint32_t published_ = 0;

    ResetAction reset_action_{*this};
    MainMenuAction main_menu_action_{*this};
    HdCloseRow resume_row_;
    HdGatedRow resume_gate_{resume_row_, resume_};
    LoadRow load_row_{*this};
    HdPushRow media_row_;
    HdGatedRow media_gate_{media_row_, has_media_};
    HdPushRow recent_row_;
    HdGatedRow recent_gate_{recent_row_, has_recent_};
    HdPushRow cheats_row_;
    HdGatedRow cheats_gate_{cheats_row_, cheats_};
    HdAskRow reset_row_;
    HdGatedRow reset_gate_{reset_row_, reset_};
    HdSeparatorRow separator_;
    HdPushRow core_settings_row_;
    HdGatedRow core_settings_gate_{core_settings_row_, core_settings_};
    HdPushRow settings_row_;
    HdAskRow main_menu_row_;
    HdGatedRow main_menu_gate_{main_menu_row_, main_menu_};
    std::vector<std::reference_wrapper<IHdRow>> refs_;
};

}
