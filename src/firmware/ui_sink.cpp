// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui_sink.h"

#include <sys/statvfs.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string_view>

#include "app/event.h"
#include "app/event_dispatch.h"
#include "app/identity_latch.h"
#include "infra/seat.h"
#include "app/input_wire.h"
#include "app/mgl_pump.h"
#include "app/process_integration.h"
#include "app/video_pump.h"
#include "host_facts.h"
#include "os/bt_probe.h"
#include "os/clock.h"
#include "os/net_probe.h"
#include "os/process_title.h"
#include "svc/vfs.h"
#include "ui/event_trace.h"
#include "ui/menu_model.h"
#include "ui/osd_renderer.h"
#include "ui/page_context.h"

namespace mister::fw {

namespace {

struct HandoffPath {
    char buf[128]{};

    HandoffPath(const char* root, const char* name) noexcept {
        (void)std::snprintf(buf, sizeof buf, "%s/%s", root, name);
    }
};

constexpr std::string_view kTitlePrefix = "MiSTer: ";
constexpr std::size_t kTitleCap = kTitlePrefix.size() + app::SessionIdentity::kMax;

}

void UiSink::publish_conf_str() {
    TASTY_SEAT_BODY(UiSink);
    std::string_view text{};
    if (w_.conf_str.sample_into(conf_text_) != 0
        && !conf_text_.truncated
        && conf_text_.len != 0
        && conf_text_.len <= sizeof conf_text_.text) {
        text = std::string_view{conf_text_.text, conf_text_.len};
    }
    const HandoffPath p{w_.tmp_root, "CONFSTR"};
    if (!app::proc::write_tmp_handoff(p.buf, text).has_value())
        ++corename_failures_;
}

void UiSink::stamp_core_identity(bool front_end) {

    TASTY_SEAT_BODY(UiSink);

    publish_conf_str();
    app::SessionIdentity id{};
    if (!w_.identity.copy(id)) {
        ++corename_failures_;
        w_.video.set_core_name(std::string_view{});
        return;
    }
    w_.video.set_core_name(id.core);
    if (w_.title.has_value()) {

        std::array<char, kTitleCap> title{};
        const char* const name = front_end ? "menu" : id.core;
        std::size_t n = 0;
        for (const char c : kTitlePrefix)
            title[n++] = c;
        for (std::size_t i = 0; name[i] != '\0' && n + 1 < title.size(); ++i)
            title[n++] = name[i];
        title[n] = '\0';
        w_.title->set(std::string_view{title.data(), n});
    }
    const HandoffPath core{w_.tmp_root, "CORENAME"};
    const HandoffPath rbf{w_.tmp_root, "RBFNAME"};
    const bool a = app::proc::write_tmp_handoff(core.buf, id.core).has_value();
    const bool b = app::proc::write_tmp_handoff(rbf.buf, id.rbf).has_value();
    if (a && b) {
        ++corename_writes_;
    } else {
        ++corename_failures_;
    }

    if (id.game_id.writes()) {
        const HandoffPath gid{w_.tmp_root, "GAMEID"};
        if (!app::proc::write_tmp_handoff(gid.buf, id.game_id.body().view()).has_value()) {
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
    if (!app::proc::write_tmp_handoff(p.buf, byte).has_value())
        ++osd_visible_failures_;
}

void UiSink::deliver(const app::Event& e) {
    TASTY_SEAT_BODY(UiSink);
    infra::dispatch<app::EventRoutes>(e, *this);
}

void UiSink::on(const app::Event::CoreLoaded& a, const app::Event::Head& h) {
    TASTY_SEAT_BODY(UiSink);
    w_.trace.record(a, h);

    stamp_core_identity(a.front_end);
    w_.mgl.on_core_loaded(h.tag, a.mgl_capable);
    w_.video.on_core_loaded(a.front_end);
    {
        app::SessionIdentity cid{};
        std::string_view core_name{};
        if (w_.identity.copy(cid)) {
            core_name = cid.core;
        }
        w_.input.on_core_loaded(core_name, cid.front_end, cid.joy, cid.analog_reshape);
    }
    w_.menu->on_core_loaded(a.type, a.front_end, w_.ctx);
    w_.menu->on_core_load_done(h.tag, w_.ctx);
}

void UiSink::session_over_(const app::Event::Head& h) {
    w_.mgl.on_session_over(h.tag);
    w_.video.on_session_over();
    w_.input.on_core_detached();
}

void UiSink::on(const app::Event::SessionFailed& a, const app::Event::Head& h) {
    w_.trace.record(a, h);
    session_over_(h);
    w_.menu->on_request_refused(h.tag, w_.ctx);
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
    w_.menu->on_request_refused(h.tag, w_.ctx);
}

void UiSink::on(const app::Event::RequestDone& a, const app::Event::Head& h) {
    w_.trace.record(a, h);
    w_.menu->on_request_done(h.tag, static_cast<std::int64_t>(std::time(nullptr)), w_.ctx);
}

void UiSink::on(const app::Event::InfoRequest& a, const app::Event::Head& h) {
    w_.trace.record(a, h);
    w_.menu->on_info(a.id, w_.ctx);
}

void UiSink::on(const app::Event::ProgressUpdate& a, const app::Event::Head& h) {
    w_.trace.record(a, h);
    w_.menu->on_progress(a.cur, a.max, w_.ctx);
}

void UiSink::on(const app::Event::SdActivity& a, const app::Event::Head& h) {
    w_.trace.record(a, h);
}

void UiSink::on(const app::Event::DeadlineMiss& a, const app::Event::Head& h) {
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

void HostFacts::sample(ui::SystemFacts& sys, ui::StatusBarFacts& bar) {
    TASTY_SEAT_BODY(HostFacts);

    static_assert(
        sizeof(TASTY_VDATE) == 7,
        "video-rule: TASTY_VDATE is YYMMDD — six chars plus the NUL, the "
        "exact width menu.cpp renders"
    );
    std::memcpy(sys.version, TASTY_VDATE, sizeof(TASTY_VDATE));
    sys.kernel = kernel_rt_;

    sys.os_version[0] = '\0';
    if (FILE* f = std::fopen("/MiSTer.version", "r")) {
        char raw[8] = {};
        if (std::fread(raw, 6, 1, f) == 1) {
            for (std::size_t i = 0; i + 1 < sizeof sys.os_version; ++i) {
                sys.os_version[i] = raw[i];
            }
            sys.os_version[sizeof sys.os_version - 1] = '\0';
        }
        (void)std::fclose(f);
    }

    struct statvfs st{};
    if (::statvfs(vfs_.root_path().c_str(), &st) == 0) {
        sys.avail_bytes = static_cast<std::uint64_t>(st.f_bsize)
            * static_cast<std::uint64_t>(st.f_bavail);
    }
    sys.storage_usb = vfs_.root().usb;

    const std::time_t now = std::time(nullptr);
    struct tm tmv{};
    if (::localtime_r(&now, &tmv) != nullptr) {
        (void)std::strftime(bar.clock, sizeof bar.clock, "%b %d %a%H:%M:%S", &tmv);
    }
    bar.splash = !splash_spent_;
    splash_spent_ = true;

    const bool pairing = menu_.bt_pairing_active();
    if (!pairing) {
        const auto mono = clock_.now();
        if (!bt_probed_ || mono - bt_probed_at_ >= std::chrono::seconds{3}) {
            bt_probed_ = true;
            bt_probed_at_ = mono;
            if (auto up = os::bluetooth_adapter_up())
                bt_up_ = *up;
        }
    }
    bar.bluetooth = bt_up_;

    const auto mono = clock_.now();
    if (!net_probed_ || mono - net_probed_at_ >= std::chrono::seconds{3}) {
        net_probed_ = true;
        net_probed_at_ = mono;
        if (auto np = os::net_presence()) {
            net_wired_ = np->wired;
            net_wifi_ = np->wifi;
        }
    }
    bar.net_wired = net_wired_;
    bar.net_wifi = net_wifi_;
}

}
