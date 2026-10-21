// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "app/link_tx_channel.h"
#include "app/option_cell.h"
#include "app/page_description.h"
#include "app/recents_store.h"
#include "app/ui_request_ring.h"
#include "proto/reset_edge.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "ui/browser_host.h"
#include "ui/browser_session.h"
#include "ui/page.h"

namespace mister::ui {

struct BrowserLaunch;

class CoreSettingsPage final : public Page, public IBrowserHost {
    TASTY_SEAT_RESIDENT(Ui);

public:
    struct Spec {

        const app::OptionTable* table;
        std::uint8_t page = 0;
        std::string title;

        bool mt32_available = false;

        bool joyswap = false;

        std::string_view custom_aspect_ratio[2]{};

        proto::BindGeneration conf_gen{};
    };

    explicit CoreSettingsPage(Spec s);

    PageId id() const override {
        TASTY_SEAT_BODY(CoreSettingsPage);
        return PageId::CoreSettings;
    }

    std::uint8_t page() const noexcept { return page_; }

    bool empty_page() const noexcept { return selectable_ == 0; }

    void on_enter(PageContext& ctx) override;
    void render(OsdRenderer& out) const override;

    bool describe(app::PageDescription& out) const override;
    Transition handle(const UiEvent& ev) override;

    [[nodiscard]] std::string row_text(std::uint32_t drawn) const;

    [[nodiscard]] std::uint32_t drawn_rows() const noexcept { return exit_drawn() + 1; }

    [[nodiscard]] std::uint32_t selectable_rows() const noexcept { return selectable_; }

    [[nodiscard]] std::uint32_t first_row() const noexcept { return first_row_; }

    MenuMask mask() const override;

    void set_selection(RowIndex i) override {
        TASTY_SEAT_BODY(CoreSettingsPage);
        sel_ = i;
        track_viewport();
    }

    RowIndex selection() const override {
        TASTY_SEAT_BODY(CoreSettingsPage);
        return sel_;
    }

    Transition on_picked(const BrowserEntry& e, std::string_view path) override;
    Transition on_cancel() override;
    Transition on_eject() override;

    std::uint32_t refusals() const noexcept override {
        TASTY_SEAT_BODY(CoreSettingsPage);
        return refusals_;
    }

    std::uint32_t commands_published() const noexcept { return published_; }

    bool reset_armed() const noexcept { return armed_; }

private:
    struct Line {
        std::uint32_t ordinal = 0;
        std::uint8_t index = 0;
        bool selectable = false;
    };

    void rebuild();
    void ask_mount(std::uint8_t slot, std::string_view path);

    void ask_reset(proto::ResetEdge edge);
    template <class A>
    void push_op(const A& op);
    void track_viewport() noexcept;

    template <class Visit>
    void for_each_row(Visit&& visit) const;

    [[nodiscard]] std::string compose(const Line& l) const;

    [[nodiscard]] std::string cycle_value(const app::OptionTable::Row& r) const;

    [[nodiscard]] std::string value_label(const cores::OptionRow& d, std::uint32_t slot) const;
    [[nodiscard]] std::uint32_t drawn_choice(const app::OptionTable::Row& r) const;
    [[nodiscard]] const Line* line_for_ordinal(std::uint32_t o) const noexcept;
    [[nodiscard]] std::uint32_t exit_drawn() const noexcept;
    [[nodiscard]] BrowserSession::Request browser_request(const app::OptionTable::Row& r);
    Transition open_browser(const app::OptionTable::Row& r, bool recents_first);
    Transition leave();

    const app::OptionTable* table_;
    std::uint8_t page_;
    std::string title_;
    bool mt32_available_;
    bool joyswap_;
    std::string_view arc_[2]{};

    std::vector<Line> lines_;
    std::uint32_t selectable_ = 0;

    infra::OptRef<app::UiRequestRing> asks_{};
    proto::BindGeneration conf_gen_{};
    infra::OptRef<app::LinkTxChannel<SeatTag::Ui>> link_tx_{};
    infra::OptRef<BrowserLaunch> browser_{};
    infra::OptRef<app::RecentsStore> recents_{};
    std::string core_dir_;

    RowIndex sel_{};
    std::uint32_t first_row_ = 0;
    bool armed_ = false;
    std::uint8_t pending_slot_ = 0;
    std::uint32_t refusals_ = 0;
    std::uint32_t published_ = 0;
};

}
