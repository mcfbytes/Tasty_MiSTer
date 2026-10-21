// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/conf_str_cell.h"
#include "app/event.h"
#include "app/event_sink.h"
#include "ui/osd_visible_file.h"
#include "ui/ui_round.h"
#include "hal/thread_map.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::ui {
class EventTrace;
class MenuModel;
struct PageContext;
}

namespace mister::app {
class IdentityLatch;
class InputWire;
class MglPump;
class VideoPump;
}

namespace mister::os {
class ProcessTitle;
}

namespace mister::fw {

class UiSink : public app::IEventSink, public ui::IOsdVisibleFile {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr SeatTag kSeat = SeatTag::Ui;

    struct Wiring {
        infra::OptRef<ui::MenuModel> menu{};
        ui::PageContext& ctx;
        ui::EventTrace& trace;
        app::MglPump& mgl;
        app::VideoPump& video;

        app::InputWire& input;

        const app::IdentityLatch& identity;

        const app::ConfStrCell& conf_str;

        const char* tmp_root = "/tmp";

        infra::OptRef<os::ProcessTitle> title{};
    };

    explicit UiSink(const Wiring& w) noexcept : w_(w) {}

    UiSink(const UiSink&) = delete;
    UiSink& operator=(const UiSink&) = delete;

    void deliver(const app::Event& e) override;

    void on(const app::Event::CoreLoaded& a, const app::Event::Head& h);
    void on(const app::Event::SessionFailed& a, const app::Event::Head& h);
    void on(const app::Event::SdActivity& a, const app::Event::Head& h);
    void on(const app::Event::InfoRequest& a, const app::Event::Head& h);
    void on(const app::Event::ProgressUpdate& a, const app::Event::Head& h);
    void on(const app::Event::DeadlineMiss& a, const app::Event::Head& h);
    void on(const app::Event::RequestRefused& a, const app::Event::Head& h);
    void on(const app::Event::ConfStrOnlySession& a, const app::Event::Head& h);
    void on(const app::Event::SessionAdvisory& a, const app::Event::Head& h);
    void on(const app::Event::SessionEnded& a, const app::Event::Head& h);
    void on(const app::Event::RamImageDeclined& a, const app::Event::Head& h);
    void on(const app::Event::RequestDone& a, const app::Event::Head& h);

    void misrouted(const app::Event& e) noexcept;

    [[nodiscard]] std::uint32_t event_misrouted() const noexcept { return event_misrouted_; }

    void set_log_file_entry(bool on) noexcept { log_file_entry_ = on; }

    void on_visible(bool on) override;

private:
    void stamp_core_identity(bool front_end);
    void publish_conf_str();
    void session_over_(const app::Event::Head& h);

    Wiring w_;

    app::ConfStrText conf_text_{};
    std::uint32_t corename_writes_ = 0;
    std::uint32_t corename_failures_ = 0;
    bool log_file_entry_ = false;
    std::uint32_t osd_visible_failures_ = 0;
    std::uint32_t event_misrouted_ = 0;
};

}
