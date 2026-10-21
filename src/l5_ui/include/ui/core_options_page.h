// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "app/link_tx_channel.h"
#include "app/ui_request_ring.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "proto/conf_str.h"
#include "ui/build_env.h"
#include "ui/classic_options_view.h"
#include "ui/hd_options_view.h"
#include "ui/options_surface.h"
#include "ui/options_view.h"
#include "ui/options_model.h"
#include "ui/options_page.h"
#include "ui/page.h"
#include "ui/slot_pick_host.h"
#include "ui/status_view.h"
#include "ui/types.h"

namespace mister::ui {

class CoreOptionsPage final : public Page, public IMaskRebuild, private IOptionsSurface {
    TASTY_SEAT_RESIDENT(Ui);

public:
    CoreOptionsPage(
        const proto::ItemTable& table,
        const BuildEnv& env,
        std::string_view title,
        infra::OptRef<const StatusView> status = {}
    );

    CoreOptionsPage(
        const proto::ItemTable& table,
        const BuildEnv& env,
        std::string_view title,
        infra::OptRef<const StatusView> status,
        const HdOptionsView::Spec& hd
    );
    ~CoreOptionsPage() override;

    PageId id() const override {
        TASTY_SEAT_BODY(CoreOptionsPage);
        return view_->id();
    }

    void on_enter(PageContext& ctx) override;
    void render(OsdRenderer& out) const override;

    bool describe(app::PageDescription& out) const override;
    Transition handle(const UiEvent& ev) override;

    MenuMask mask() const override {
        TASTY_SEAT_BODY(CoreOptionsPage);
        return view_->mask();
    }

    void set_selection(RowIndex i) override {
        TASTY_SEAT_BODY(CoreOptionsPage);
        view_->set_selection(i);
    }

    RowIndex selection() const override {
        TASTY_SEAT_BODY(CoreOptionsPage);
        return view_->selection();
    }

    RowCount osd_height() const override {
        TASTY_SEAT_BODY(CoreOptionsPage);
        return ClassicOptionsView::kOsdRows;
    }

    infra::OptRef<IMaskRebuild> mask_rebuild() override {
        TASTY_SEAT_BODY(CoreOptionsPage);
        return infra::OptRef<IMaskRebuild>{*this};
    }

    std::uint8_t page() const noexcept override {
        TASTY_SEAT_BODY(CoreOptionsPage);
        return model_.page();
    }

    std::uint32_t status_field(BitField b) const noexcept {
        return status_ ? status_->field(b) : 0u;
    }

    const OptionsModel& model() const noexcept { return model_; }

    void rebuild(const proto::ItemTable& table, const BuildEnv& env) override;

    std::string compose(const OptionItem& it) const;

    [[nodiscard]] bool empty() const {
        TASTY_SEAT_BODY(CoreOptionsPage);
        return view_->empty();
    }

    std::uint32_t refusals() const noexcept override {
        TASTY_SEAT_BODY(CoreOptionsPage);
        return refusals_ + view_->refusals() + (picks_ ? picks_->refusals() : 0u);
    }

    std::uint32_t commands_published() const noexcept {
        return published_ + (picks_ ? picks_->commands_published() : 0u);
    }

    std::uint32_t first_row() const noexcept { return view_->first_row(); }

private:
    void first_walk_(const proto::ItemTable& table, const BuildEnv& env);

    void refuse() override;
    void write_status(BitField bits, std::uint32_t value) override;
    void toggle(BitField bits, bool ex) override;

    Transition open_browser(const OptionItem& it) override;

    const OptionsModel& walk() const override;
    std::string_view heading() const override;
    std::uint32_t field(BitField bits) const override;
    StatusView live_words() const override;
    Transition open_recents(const OptionItem& it) override;
    void save_config() override;

    template <class A>
    void push_op(const A& op);

    OptionsModel model_;

    std::string title_;

    SlotPickHost::Env env_;
    infra::OptRef<const StatusView> status_;
    infra::OptRef<app::UiRequestRing> asks_{};
    infra::OptRef<app::LinkTxChannel<SeatTag::Ui>> link_tx_{};

    std::optional<SlotPickHost> picks_;
    std::uint32_t refusals_ = 0;
    std::uint32_t published_ = 0;

    std::unique_ptr<IOptionsView> view_;
};

}
