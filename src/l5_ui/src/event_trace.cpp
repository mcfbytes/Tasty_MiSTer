// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/event_trace.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <iterator>

#include "app/ui_request.h"

namespace mister::ui {

namespace {

constexpr std::size_t kEventFieldBytes = 96;

struct RequestName {
    app::UiRequest::Kind kind;
    const char* name;
};

constexpr RequestName kRequestNames[] = {
    {app::UiRequest::Kind::LoadCore, "LoadCore"},
    {app::UiRequest::Kind::SaveConfig, "SaveConfig"},
    {app::UiRequest::Kind::SaveDips, "SaveDips"},
    {app::UiRequest::Kind::SaveCoreConfig, "SaveCoreConfig"},
    {app::UiRequest::Kind::LoadCoreConfig, "LoadCoreConfig"},
    {app::UiRequest::Kind::LoadFile, "LoadFile"},
    {app::UiRequest::Kind::MountImage, "MountImage"},
    {app::UiRequest::Kind::UnmountImage, "UnmountImage"},
    {app::UiRequest::Kind::ResetCore, "ResetCore"},
    {app::UiRequest::Kind::Reboot, "Reboot"},
    {app::UiRequest::Kind::LoadFileByDigit, "LoadFileByDigit"},
    {app::UiRequest::Kind::LoadRamImage, "LoadRamImage"},
    {app::UiRequest::Kind::SetOsdStyle, "SetOsdStyle"},
    {app::UiRequest::Kind::SdramDetected, "SdramDetected"},
    {app::UiRequest::Kind::SetHdLook, "SetHdLook"},
};

consteval bool request_names_are_ordinal() {
    if (std::size(kRequestNames) != infra::ordinal(app::UiRequest::Kind::kCount))
        return false;
    for (std::size_t i = 0; i < std::size(kRequestNames); ++i) {
        if (infra::ordinal(kRequestNames[i].kind) != i)
            return false;
    }
    return true;
}

static_assert(request_names_are_ordinal(), "UiRequest::Kind changed: name it here at its ordinal");

struct ErrcName {
    Errc errc;
    const char* name;
};

constexpr auto kErrcNames = std::to_array<ErrcName>({
    {Errc::spi_timeout, "spi_timeout"},
    {Errc::spi_nak, "spi_nak"},
    {Errc::fpga_not_ready, "fpga_not_ready"},
    {Errc::io, "io"},
    {Errc::not_found, "not_found"},
    {Errc::short_read, "short_read"},
    {Errc::short_write, "short_write"},
    {Errc::bad_format, "bad_format"},
    {Errc::mount_failed, "mount_failed"},
    {Errc::bad_confstr, "bad_confstr"},
    {Errc::bad_opcode, "bad_opcode"},
    {Errc::slot_range, "slot_range"},
    {Errc::negotiation, "negotiation"},
    {Errc::mmap_failed, "mmap_failed"},
    {Errc::uio_open, "uio_open"},
    {Errc::dt_missing, "dt_missing"},
    {Errc::bridge_state, "bridge_state"},
    {Errc::os, "os"},
    {Errc::core_load, "core_load"},
    {Errc::cancelled, "cancelled"},
    {Errc::would_block, "would_block"},
    {Errc::timeout, "timeout"},
    {Errc::unimplemented, "unimplemented"},
    {Errc::aperture_range, "aperture_range"},
    {Errc::stale, "stale"},
    {Errc::busy, "busy"},
    {Errc::seat_absent, "seat_absent"},
    {Errc::contract, "contract"},
});

consteval bool errc_names_are_ordinal() {
    if (std::size(kErrcNames) != infra::ordinal(Errc::contract) + 1)
        return false;
    for (std::size_t i = 0; i < std::size(kErrcNames); ++i) {
        if (infra::ordinal(kErrcNames[i].errc) != i)
            return false;
    }
    return true;
}

static_assert(errc_names_are_ordinal(), "Errc changed: name it here at its ordinal (item)");

const char* errc_name(Errc c) noexcept {
    const std::size_t i = infra::ordinal(c);
    return i < std::size(kErrcNames) ? kErrcNames[i].name : "?";
}

const char* request_name(app::UiRequest::Kind k) noexcept {
    const std::size_t i = infra::ordinal(k);
    return i < std::size(kRequestNames) ? kRequestNames[i].name : "?";
}

std::int64_t monotonic_ns() noexcept {
    return std::chrono::steady_clock::now().time_since_epoch().count();
}

}

bool EventTrace::admit_() noexcept {
    const std::int64_t now = monotonic_ns();
    constexpr std::int64_t kWindowNs = std::chrono::nanoseconds{std::chrono::seconds{1}}.count();
    if (now - window_start_ns_ >= kWindowNs) {
        window_start_ns_ = now;
        window_count_ = 0;
    }
    if (window_count_ >= kMaxPerSec) {
        ++suppressed_;
        return false;
    }
    ++window_count_;
    return true;
}

void EventTrace::write_(
    const char* name,
    const char* fields,
    app::Event::Kind k,
    const app::Event::Head& h
) noexcept {
    const bool main_lane = lane_ == SeatTag::Main && main_.has_value();
    const auto depth = static_cast<unsigned>(main_lane ? main_->depth() : rt_->depth());
    const auto lost = main_lane ? main_->losses().of(k) : rt_->losses().of(k);
    log_.appendf(
        "{\"t\":\"ev\",\"k\":\"%s\",%s"
        "\"site\":%u,\"tag\":%u,\"depth\":%u,\"lost\":%u,"
        "\"suppressed\":%u,\"seq\":%u}",
        name,
        fields,
        static_cast<unsigned>(h.site),
        static_cast<unsigned>(h.tag.v),
        depth,
        static_cast<unsigned>(lost),
        static_cast<unsigned>(suppressed_),
        static_cast<unsigned>(seq_)
    );
    ++seq_;
}

void EventTrace::record(const app::Event::CoreLoaded& a, const app::Event::Head& h) noexcept {
    if (!admit_())
        return;
    char f[kEventFieldBytes];
    std::snprintf(
        f,
        sizeof f,
        "\"type\":%u,\"mgl\":%u,\"fe\":%u,",
        static_cast<unsigned>(a.type),
        a.mgl_capable ? 1u : 0u,
        a.front_end ? 1u : 0u
    );
    write_("CoreLoaded", f, app::Event::CoreLoaded::kKind, h);
}

void EventTrace::record(const app::Event::SessionFailed& a, const app::Event::Head& h) noexcept {
    if (!admit_())
        return;
    char f[kEventFieldBytes];
    std::snprintf(f, sizeof f, "\"errc\":\"%s\",", errc_name(a.why));
    write_("SessionFailed", f, app::Event::SessionFailed::kKind, h);
}

void EventTrace::record(const app::Event::SdActivity& a, const app::Event::Head& h) noexcept {
    if (!admit_())
        return;
    char f[kEventFieldBytes];
    std::snprintf(f, sizeof f, "\"slot\":%s,", a.index.text().c_str());
    write_("SdActivity", f, app::Event::SdActivity::kKind, h);
}

void EventTrace::record(const app::Event::InfoRequest& a, const app::Event::Head& h) noexcept {
    if (!admit_())
        return;
    char f[kEventFieldBytes];
    std::snprintf(f, sizeof f, "\"id\":%u,", static_cast<unsigned>(a.id));
    write_("InfoRequest", f, app::Event::InfoRequest::kKind, h);
}

void EventTrace::record(const app::Event::ProgressUpdate& a, const app::Event::Head& h) noexcept {
    if (!admit_())
        return;
    char f[kEventFieldBytes];
    std::snprintf(
        f,
        sizeof f,
        "\"cur\":%u,\"max\":%u,",
        static_cast<unsigned>(a.progress.done),
        static_cast<unsigned>(a.progress.total)
    );
    write_("ProgressUpdate", f, app::Event::ProgressUpdate::kKind, h);
}

void EventTrace::record(const app::Event::RequestRefused& a, const app::Event::Head& h) noexcept {
    if (!admit_())
        return;
    char f[kEventFieldBytes];
    std::snprintf(
        f,
        sizeof f,
        "\"req\":\"%s\",\"errc\":\"%s\",",
        request_name(a.which),
        errc_name(a.why)
    );
    write_("RequestRefused", f, app::Event::RequestRefused::kKind, h);
}

void EventTrace::record(
    const app::Event::ConfStrOnlySession& a,
    const app::Event::Head& h
) noexcept {
    if (!admit_())
        return;
    char f[kEventFieldBytes];
    std::snprintf(f, sizeof f, "\"cause\":%u,", static_cast<unsigned>(a.cause));
    write_("ConfStrOnlySession", f, app::Event::ConfStrOnlySession::kKind, h);
}

void EventTrace::record(const app::Event::SessionAdvisory& a, const app::Event::Head& h) noexcept {
    if (!admit_())
        return;
    char f[kEventFieldBytes];
    std::snprintf(f, sizeof f, "\"errc\":\"%s\",", errc_name(a.why));
    write_("SessionAdvisory", f, app::Event::SessionAdvisory::kKind, h);
}

void EventTrace::record(const app::Event::SessionEnded&, const app::Event::Head& h) noexcept {
    if (!admit_())
        return;
    write_("SessionEnded", "\"errc\":null,", app::Event::SessionEnded::kKind, h);
}

void EventTrace::record(const app::Event::RamImageDeclined& a, const app::Event::Head& h) noexcept {
    if (!admit_())
        return;
    char f[kEventFieldBytes];
    std::snprintf(f, sizeof f, "\"errc\":\"%s\",", errc_name(a.why));
    write_("RamImageDeclined", f, app::Event::RamImageDeclined::kKind, h);
}

void EventTrace::record(const app::Event::RequestDone& a, const app::Event::Head& h) noexcept {
    if (!admit_())
        return;
    char f[kEventFieldBytes];
    std::snprintf(f, sizeof f, "\"req\":\"%s\",", request_name(a.which));
    write_("RequestDone", f, app::Event::RequestDone::kKind, h);
}

void EventTrace::record_misrouted() noexcept {
    if (!admit_())
        return;
    const bool main_lane = lane_ == SeatTag::Main && main_.has_value();
    log_.appendf(
        "{\"t\":\"ev\",\"k\":\"?\",\"depth\":%u,"
        "\"suppressed\":%u,\"seq\":%u}",
        static_cast<unsigned>(main_lane ? main_->depth() : rt_->depth()),
        static_cast<unsigned>(suppressed_),
        static_cast<unsigned>(seq_)
    );
    ++seq_;
}

}
