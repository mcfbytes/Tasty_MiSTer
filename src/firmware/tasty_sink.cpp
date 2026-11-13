// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_sink.h"

#include <string_view>

#include "app/event_dispatch.h"
#include "app/identity_latch.h"
#include "app/session_identity.h"
#include "app/video_pump.h"
#include "infra/error.h"
#include "infra/message_sum.h"
#include "tasty_say.h"

namespace mister::fw {

void TastySink::deliver(const app::Event& e) {
    TASTY_SEAT_BODY(TastySink);
    infra::dispatch<app::EventRoutes>(e, *this);
}

void TastySink::on(const app::Event::CoreLoaded& a, const app::Event::Head&) {
    TASTY_SEAT_BODY(TastySink);
    const std::optional<app::SessionIdentity> id = identity_ ? identity_->copy() : std::nullopt;
    video_.set_core_name(id ? id->core.view() : std::string_view{});
    video_.set_spd_identity(
        id ? app::home_name(*id) : std::string_view{},
        id ? id->rotation : proto::RotationDir::None
    );
    video_.on_core_loaded(a.front_end);
}

void TastySink::on(const app::Event::SessionFailed&, const app::Event::Head&) {
    video_.on_session_over();
}

void TastySink::on(const app::Event::SessionEnded&, const app::Event::Head&) {
    video_.on_session_over();
}

void TastySink::on(const app::Event::SdActivity&, const app::Event::Head&) {
    note_unhandled_();
}

void TastySink::on(const app::Event::InfoRequest&, const app::Event::Head&) {
    note_unhandled_();
}

void TastySink::on(const app::Event::ProgressUpdate&, const app::Event::Head&) {
    note_unhandled_();
}

void TastySink::on(const app::Event::RequestRefused&, const app::Event::Head&) {
    note_unhandled_();
}

void TastySink::on(const app::Event::RamImageDeclined& a, const app::Event::Head&) {
    if (ram_image_said_)
        return;
    ram_image_said_ = true;
    tasty_say(
        a.why == Errc::negotiation
            ? "tasty: this core takes no power-on RAM image; the game starts from the "
              "core's own RAM fill"
            : "tasty: the power-on RAM image was not sent; the game starts from the "
              "core's own RAM fill"
    );
}

void TastySink::on(const app::Event::ConfStrOnlySession&, const app::Event::Head&) {
    note_unhandled_();
}

void TastySink::on(const app::Event::RequestDone&, const app::Event::Head&) {
    note_unhandled_();
}

void TastySink::on(const app::Event::SessionAdvisory&, const app::Event::Head&) {
    note_unhandled_();
}

void TastySink::note_unhandled_() noexcept {
    ++event_unhandled_drops_;
}

void TastySink::misrouted(const app::Event&) noexcept {
    TASTY_SEAT_BODY(TastySink);
    ++event_misrouted_;
}

static_assert(app::EventUiSink<TastySink>);

}
