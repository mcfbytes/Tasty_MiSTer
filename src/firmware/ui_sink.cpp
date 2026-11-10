// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_sink.h"

#include <ctime>
#include <span>
#include <string_view>

#include "app/event.h"
#include "app/event_dispatch.h"
#include "app/identity_latch.h"
#include "app/input_wire.h"
#include "app/mgl_pump.h"
#include "app/process_integration.h"
#include "app/video_pump.h"
#include "infra/fixed_str.h"
#include "infra/path_text.h"
#include "infra/seat.h"
#include "os/path_buf.h"
#include "os/process_title.h"
#include "ui/event_trace.h"
#include "ui/menu_model.h"
#include "ui/page_context.h"

namespace mister::fw {

namespace {

struct HandoffPath {
    char buf[infra::kPathMax]{};
    bool built = false;

    HandoffPath(const char* root, const char* name) noexcept
        : built{os::format_path(std::span(buf), "%s/%s", root, name)} {}

    [[nodiscard]] bool write(std::string_view text) const {
        return built && app::proc::write_tmp_handoff(buf, text).has_value();
    }
};

constexpr std::string_view kTitlePrefix = "MiSTer: ";
constexpr std::size_t kTitleCap = kTitlePrefix.size() + app::SessionIdentity::kMax;

}

void UiSink::publish_conf_str() {
    TASTY_SEAT_BODY(UiSink);
    std::string_view text{};
    if (w_.conf_str.sample_into(conf_text_) != 0 && !conf_text_.truncated) {
        text = conf_text_.view();
    }
    const HandoffPath p{w_.tmp_root, "CONFSTR"};
    if (!p.write(text))
        ++corename_failures_;
}

void UiSink::stamp_core_identity(bool front_end) {

    TASTY_SEAT_BODY(UiSink);

    publish_conf_str();
    const std::optional<app::SessionIdentity> id = w_.identity.copy();
    if (!id) {
        ++corename_failures_;
        w_.video.set_core_name(std::string_view{});
        return;
    }
    w_.video.set_core_name(id->core.view());
    if (w_.title.has_value()) {

        FixedStr<kTitleCap, StrFit::Clip> title;
        (void)title.assign(kTitlePrefix);
        (void)title.append(front_end ? std::string_view{"menu"} : id->core.view());
        w_.title->set(title.view());
    }
    const HandoffPath core{w_.tmp_root, "CORENAME"};
    const HandoffPath rbf{w_.tmp_root, "RBFNAME"};
    const bool a = core.write(id->core.view());
    const bool b = rbf.write(id->rbf.view());
    if (a && b) {
        ++corename_writes_;
    } else {
        ++corename_failures_;
    }

    if (id->game_id.writes()) {
        const HandoffPath gid{w_.tmp_root, "GAMEID"};
        if (!gid.write(id->game_id.body().view())) {
            ++corename_failures_;
        }
    }
}

void UiSink::on_visible(bool on) {
    TASTY_SEAT_BODY(UiSink);
    if (!log_file_entry_)
        return;
    const HandoffPath p{w_.tmp_root, "OSD_VISIBLE"};
    const std::string_view byte = on ? std::string_view{"1"} : std::string_view{"0"};
    if (!p.write(byte))
        ++osd_visible_failures_;
}

void UiSink::deliver(const app::Event& e) {
    TASTY_SEAT_BODY(UiSink);

    w_.trace.lane(SeatTag::RT);
    infra::dispatch<app::EventRoutes>(e, *this);
}

void UiSink::deliver_from(const app::Event& e, SeatTag from) {
    TASTY_SEAT_BODY(UiSink);
    w_.trace.lane(from);
    infra::dispatch<app::EventRoutes>(e, *this);
}

void UiSink::on(const app::Event::CoreLoaded& a, const app::Event::Head& h) {
    TASTY_SEAT_BODY(UiSink);
    w_.trace.record(a, h);

    stamp_core_identity(a.front_end);
    w_.mgl.on_core_loaded(h.tag, a.mgl_capable);
    w_.video.on_core_loaded(a.front_end);
    {

        const app::SessionIdentity cid = w_.identity.copy().value_or(app::SessionIdentity{});
        w_.input.on_core_loaded(cid.core.view(), cid.front_end, cid.joy, cid.analog_reshape);
    }
    w_.menu.on_core_loaded(a.type, a.front_end, w_.ctx);
    w_.menu.on_core_load_done(h.tag, w_.ctx);
}

void UiSink::session_over_(const app::Event::Head& h) {
    w_.mgl.on_session_over(h.tag);
    w_.video.on_session_over();
    w_.input.on_core_detached();
}

void UiSink::on(const app::Event::SessionFailed& a, const app::Event::Head& h) {
    w_.trace.record(a, h);
    session_over_(h);
    w_.menu.on_request_refused(h.tag, w_.ctx);
}

void UiSink::on(const app::Event::SessionEnded& a, const app::Event::Head& h) {
    w_.trace.record(a, h);
    session_over_(h);
}

void UiSink::on(const app::Event::SessionAdvisory& a, const app::Event::Head& h) {
    w_.trace.record(a, h);
}

void UiSink::on(const app::Event::RamImageDeclined& a, const app::Event::Head& h) {
    w_.trace.record(a, h);
}

void UiSink::on(const app::Event::RequestRefused& a, const app::Event::Head& h) {
    w_.trace.record(a, h);
    w_.mgl.on_request_refused(h.tag, a.why);
    w_.menu.on_request_refused(h.tag, w_.ctx);
}

void UiSink::on(const app::Event::RequestDone& a, const app::Event::Head& h) {
    w_.trace.record(a, h);
    w_.menu.on_request_done(h.tag, static_cast<std::int64_t>(std::time(nullptr)), w_.ctx);
}

void UiSink::on(const app::Event::InfoRequest& a, const app::Event::Head& h) {
    w_.trace.record(a, h);
    w_.menu.on_info(a.id, w_.ctx);
}

void UiSink::on(const app::Event::ProgressUpdate& a, const app::Event::Head& h) {
    w_.trace.record(a, h);
    w_.menu.on_progress(a.progress, w_.ctx);
}

void UiSink::on(const app::Event::SdActivity& a, const app::Event::Head& h) {
    w_.trace.record(a, h);
}

void UiSink::on(const app::Event::ConfStrOnlySession& a, const app::Event::Head& h) {
    w_.trace.record(a, h);
}

void UiSink::misrouted(const app::Event&) noexcept {
    TASTY_SEAT_BODY(UiSink);
    ++event_misrouted_;
    w_.trace.record_misrouted();
}

static_assert(app::EventUiSink<UiSink>);

}
