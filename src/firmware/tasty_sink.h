// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/event.h"
#include "app/event_sink.h"
#include "infra/seat.h"

namespace mister::app {
class IdentityLatch;
class VideoPump;
}  // namespace mister::app

namespace mister::fw {

class TastySink final : public app::IEventSink {
    TASTY_SEAT_RESIDENT(Ui);

public:
    TastySink(app::VideoPump& video, const app::IdentityLatch* identity) noexcept
        : video_(&video), identity_(identity) {}
    TastySink(const TastySink&) = delete;
    TastySink& operator=(const TastySink&) = delete;

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
    void misrouted(const app::Event& e) noexcept;
    [[nodiscard]] std::uint32_t event_misrouted() const noexcept { return event_misrouted_; }

private:
    app::VideoPump* video_;
    const app::IdentityLatch* identity_;
    std::uint32_t event_misrouted_ = 0;
    bool ram_image_said_ = false;
};

}  // namespace mister::fw
