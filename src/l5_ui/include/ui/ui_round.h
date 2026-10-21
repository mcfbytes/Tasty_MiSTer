// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/conf_str_cell.h"
#include "app/info_decoder.h"
#include "app/input_wire.h"
#include "app/link_tx_channel.h"
#include "app/osd_wire.h"
#include "app/ui_round_port.h"
#include "os/clock.h"
#include "proto/link_event.h"
#include "ui/browser_launch.h"
#include "ui/confirm_launch.h"
#include "ui/key_decoder.h"
#include "ui/menu_model.h"
#include "ui/navigator.h"
#include "ui/osd_renderer.h"
#include "ui/page_context.h"
#include "ui/script_launch.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::app {
class HdOsdScreen;
}

namespace mister::ui {

class IHostSampler;
class IOsdVisibleFile;

class UiRound : public app::IUiRoundPort {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr unsigned kKeyDrainBudget = 16;

    struct Wiring {
        const IBrowseSource& browse;
        app::LinkTxChannel<SeatTag::Ui>& link_tx;
        IHostSampler& sampler;
        IOsdVisibleFile& osd_file;
        const app::ConfStrCell& conf_str;

        infra::OptRef<app::HdOsdScreen> hd{};
    };

    UiRound(
        app::OsdWire& osd,
        app::InputWire& input,
        const os::IClock& clock,
        MenuModel& model,
        PageContext& ctx,
        Wiring w
    ) noexcept
        : model_(model),
          renderer_(osd.surface()),
          decoder_(clock),
          ctx_(ctx),
          input_(input),
          hd_(w.hd),
          ui_tx_(w.link_tx),
          clock_(clock),
          sampler_(w.sampler),
          osd_file_(w.osd_file),
          info_(w.conf_str) {
        ctx_.keys = infra::OptRef<KeyDecoder>{decoder_};
        ctx_.clock = infra::OptRef<const os::IClock>{clock};
        ctx_.launches.browser = infra::OptRef<BrowserLaunch>{launch_};
        ctx_.launches.script = infra::OptRef<ScriptLaunch>{script_launch_};
        ctx_.launches.confirm = infra::OptRef<ConfirmLaunch>{confirm_launch_};
        ctx_.input = infra::OptRef<app::InputWire>{input};
        ctx_.storage = infra::OptRef<const IBrowseSource>{w.browse};
        ctx_.link_tx = infra::OptRef<app::LinkTxChannel<SeatTag::Ui>>{ui_tx_};
    }

    UiRound(const UiRound&) = delete;
    UiRound& operator=(const UiRound&) = delete;

    void round(bool link_work) override {
        TASTY_SEAT_BODY(UiRound);
        tick(link_work);
    }

    void tick(bool link_work = false);

    MenuModel& model() noexcept { return model_; }

    const MenuModel& model() const noexcept { return model_; }

    OsdRenderer& renderer() noexcept { return renderer_; }

    KeyDecoder& decoder() noexcept { return decoder_; }

    PageContext& context() noexcept { return ctx_; }

    using Result = bool;
    bool on(const proto::LinkEvent::ButtonLevel& b) noexcept;
    bool on(const proto::LinkEvent::OsdMask& m) noexcept;
    bool on(const proto::LinkEvent::PolledWord& w);
    bool misrouted(const proto::LinkEvent& m) noexcept;

    std::uint32_t link_event_misrouted() const noexcept { return link_event_misrouted_; }

    const app::InfoDecoder& info_decoder() const noexcept { return info_; }

    std::uint32_t host_samples() const noexcept { return samples_; }

    void boot_witness() override;

    void settle() noexcept override;

    void forget_cells() noexcept override {
        TASTY_SEAT_BODY(UiRound);
        model_.forget_cells();
    }

    bool take_link(const proto::LinkEvent& ev) noexcept override;

    std::uint32_t ticks() const noexcept { return ticks_; }

    std::uint32_t pumps() const noexcept { return pumps_; }

    std::uint32_t keys_drained() const noexcept { return keys_drained_; }

    std::uint32_t tx_drops() const noexcept { return ui_tx_drops_; }

    std::uint32_t visibility_publishes() const noexcept { return vis_pubs_; }

    const app::UiPageCell& page_cell() const noexcept { return page_cell_; }

    static constexpr std::int64_t kOsdLongPressNs = 3'000'000'000;

    static constexpr std::int64_t kOsdClassicHoldNs = 1'000'000'000;
    static constexpr std::int64_t kUserLongPressNs = 1'500'000'000;

    std::uint32_t button_edges() const noexcept { return button_edges_; }

    std::uint32_t button_longpresses() const noexcept { return button_longs_; }

    std::uint32_t button_levels() const noexcept { return button_levels_; }

    std::uint32_t last_hold_ms() const noexcept { return last_hold_ms_; }

    std::uint32_t button_classic_opens() const noexcept { return button_classic_opens_; }

    std::uint32_t button_injections() const noexcept { return button_injects_; }

    std::uint32_t bt_route_refusals() const noexcept { return bt_route_refusals_; }

    std::uint32_t button_core_resets() const noexcept { return button_core_resets_; }

    std::uint32_t user_resets_asked() const noexcept { return user_resets_asked_; }

    std::uint32_t user_reset_refusals() const noexcept { return user_reset_refusals_; }

private:
    [[nodiscard]] bool publish_visibility(proto::LinkOp::OsdShow show, proto::OsdTarget target);
    void publish_page_record();

    struct PublishOnExit {

        TASTY_SEAT_RESIDENT(Ui);
        UiRound* self;

        ~PublishOnExit() { self->publish_page_record(); }
    };

    bool poll_buttons(bool link_work);
    void ask_user_reset_();

    struct ButtonMachine {
        bool last = false;
        bool consumed = false;
        std::int64_t long_at_ns = 0;

        std::int64_t pressed_at_ns = 0;
    };

    ButtonMachine osd_btn_{};
    ButtonMachine user_btn_{};
    std::uint32_t core_edges_seen_ = 0;
    bool settling_ = false;
    std::uint32_t button_core_resets_ = 0;
    std::uint32_t button_edges_ = 0;
    std::uint32_t button_longs_ = 0;
    std::uint32_t button_levels_ = 0;
    std::uint32_t last_hold_ms_ = 0;
    std::uint32_t button_classic_opens_ = 0;
    std::uint32_t button_injects_ = 0;
    std::uint32_t bt_route_refusals_ = 0;
    std::uint32_t user_resets_asked_ = 0;
    std::uint32_t user_reset_refusals_ = 0;
    bool user_release_pending_ = false;

    MenuModel& model_;
    OsdRenderer renderer_;
    KeyDecoder decoder_;
    BrowserLaunch launch_{};
    ScriptLaunch script_launch_{};
    ConfirmLaunch confirm_launch_{};
    PageContext& ctx_;
    app::InputWire& input_;

    infra::OptRef<app::HdOsdScreen> hd_{};
    app::LinkTxChannel<SeatTag::Ui>& ui_tx_;
    std::uint32_t ui_tx_drops_ = 0;
    std::uint32_t link_event_misrouted_ = 0;
    bool osd_held_ = false;
    bool user_held_ = false;
    const os::IClock& clock_;
    IHostSampler& sampler_;
    IOsdVisibleFile& osd_file_;
    app::InfoDecoder info_;
    std::uint32_t samples_ = 0;
    proto::LinkOp::OsdShow last_show_ = proto::LinkOp::OsdShow::Off;
    proto::OsdTarget last_target_ = proto::OsdTarget::All;
    bool vis_unlatched_ = false;
    std::uint32_t ticks_ = 0;
    std::uint32_t pumps_ = 0;
    std::uint32_t keys_drained_ = 0;
    std::uint32_t vis_pubs_ = 0;
    app::UiPageCell page_cell_{};
};

}
