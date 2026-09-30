// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_sink.h"

#include <string_view>

#include "app/event_dispatch.h"
#include "app/identity_latch.h"
#include "app/session_identity.h"
#include "app/video_pump.h"
#include "infra/message_sum.h"

namespace mister::fw {

void TastySink::deliver(const app::Event& e) {
    TASTY_SEAT_BODY(TastySink);
    infra::dispatch<app::EventRoutes>(e, *this);
}

void TastySink::on(const app::Event::CoreLoaded& a, const app::Event::Head&) {
    TASTY_SEAT_BODY(TastySink);
    app::SessionIdentity id{};
    if (identity_ != nullptr && identity_->copy(id)) {
        video_->set_core_name(id.core);
    } else {
        video_->set_core_name(std::string_view{});
    }
    video_->on_core_loaded(a.front_end);
}

void TastySink::on(const app::Event::SessionFailed&, const app::Event::Head&) {
    video_->on_session_over();
}

void TastySink::on(const app::Event::SessionEnded&, const app::Event::Head&) {
    video_->on_session_over();
}

void TastySink::on(const app::Event::SdActivity&, const app::Event::Head&) {}
void TastySink::on(const app::Event::InfoRequest&, const app::Event::Head&) {}
void TastySink::on(const app::Event::ProgressUpdate&, const app::Event::Head&) {}
void TastySink::on(const app::Event::DeadlineMiss&, const app::Event::Head&) {}
void TastySink::on(const app::Event::RequestRefused&, const app::Event::Head&) {}
void TastySink::on(const app::Event::ConfStrOnlySession&, const app::Event::Head&) {}
void TastySink::on(const app::Event::SessionAdvisory&, const app::Event::Head&) {}

void TastySink::misrouted(const app::Event&) noexcept { ++event_misrouted_; }

static_assert(app::EventUiSink<TastySink>);

}  // namespace mister::fw
