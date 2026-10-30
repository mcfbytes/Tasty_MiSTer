// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/event_trace.h"

#include <cstdio>
#include <ctime>
#include <iterator>

#include "app/ui_request.h"

namespace mister::ui {

namespace {

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
    if (std::size(kRequestNames) != static_cast<std::size_t>(app::UiRequest::Kind::kCount))
        return false;
    for (std::size_t i = 0; i < std::size(kRequestNames); ++i) {
        if (static_cast<std::size_t>(kRequestNames[i].kind) != i)
            return false;
    }
    return true;
}

static_assert(request_names_are_ordinal(), "UiRequest::Kind changed: name it here at its ordinal");

constexpr const char* kErrcNames[] = {
    "spi_timeout",  "spi_nak",     "fpga_not_ready", "io",
    "not_found",    "short_read",  "short_write",    "bad_format",
    "mount_failed", "bad_confstr", "bad_opcode",     "slot_range",
    "negotiation",  "mmap_failed", "uio_open",       "dt_missing",
    "bridge_state", "os",          "core_load",      "cancelled",
    "would_block",  "timeout",     "unimplemented",  "aperture_range",
    "stale",        "busy",
};
constexpr std::size_t kErrcCount = sizeof(kErrcNames) / sizeof(kErrcNames[0]);
static_assert(
    kErrcCount == static_cast<std::size_t>(Errc::busy) + 1,
    "Errc grew: name it here (item)"
);

const char* name_or(const char* const* table, std::size_t count, std::size_t idx) noexcept {
    return idx < count ? table[idx] : "?";
}

const char* errc_name(Errc c) noexcept {
    return name_or(kErrcNames, kErrcCount, static_cast<std::size_t>(c));
}

const char* request_name(app::UiRequest::Kind k) noexcept {
    const std::size_t i = infra::ordinal(k);
    return i < std::size(kRequestNames) ? kRequestNames[i].name : "?";
}

std::int64_t monotonic_ns() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<std::int64_t>(ts.tv_sec) * 1'000'000'000
        + static_cast<std::int64_t>(ts.tv_nsec);
}

}

bool EventTrace::admit_() noexcept {
    const std::int64_t now = monotonic_ns();
    if (now - window_start_ns_ >= 1'000'000'000) {
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
    const auto depth = static_cast<unsigned>(queue_->depth());
    const auto lost = queue_->losses().of(k);
    log_.appendf(
        "{\"t\":\"ev\",\"k\":\"%s\",%s"
        "\"site\":%u,\"tag\":%u,\"depth\":%u,\"lost\":%u,"
        "\"suppressed\":%u,\"seq\":%u}",
        name,
        fields,
        static_cast<unsigned>(h.site.v),
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
    char f[96];
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
    char f[96];
    std::snprintf(f, sizeof f, "\"errc\":\"%s\",", errc_name(a.why));
    write_("SessionFailed", f, app::Event::SessionFailed::kKind, h);
}

void EventTrace::record(const app::Event::SdActivity& a, const app::Event::Head& h) noexcept {
    if (!admit_())
        return;
    char f[96];
    std::snprintf(f, sizeof f, "\"slot\":%u,", static_cast<unsigned>(a.slot));
    write_("SdActivity", f, app::Event::SdActivity::kKind, h);
}

void EventTrace::record(const app::Event::InfoRequest& a, const app::Event::Head& h) noexcept {
    if (!admit_())
        return;
    char f[96];
    std::snprintf(f, sizeof f, "\"id\":%u,", static_cast<unsigned>(a.id));
    write_("InfoRequest", f, app::Event::InfoRequest::kKind, h);
}

void EventTrace::record(const app::Event::ProgressUpdate& a, const app::Event::Head& h) noexcept {
    if (!admit_())
        return;
    char f[96];
    std::snprintf(
        f,
        sizeof f,
        "\"cur\":%u,\"max\":%u,",
        static_cast<unsigned>(a.cur),
        static_cast<unsigned>(a.max)
    );
    write_("ProgressUpdate", f, app::Event::ProgressUpdate::kKind, h);
}

void EventTrace::record(const app::Event::DeadlineMiss& a, const app::Event::Head& h) noexcept {
    if (!admit_())
        return;
    char f[96];
    std::snprintf(f, sizeof f, "\"slot\":%u,", static_cast<unsigned>(a.slot));
    write_("DeadlineMiss", f, app::Event::DeadlineMiss::kKind, h);
}

void EventTrace::record(const app::Event::RequestRefused& a, const app::Event::Head& h) noexcept {
    if (!admit_())
        return;
    char f[96];
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
    char f[96];
    std::snprintf(f, sizeof f, "\"cause\":%u,", static_cast<unsigned>(a.cause));
    write_("ConfStrOnlySession", f, app::Event::ConfStrOnlySession::kKind, h);
}

void EventTrace::record(const app::Event::SessionAdvisory& a, const app::Event::Head& h) noexcept {
    if (!admit_())
        return;
    char f[96];
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
    char f[96];
    std::snprintf(f, sizeof f, "\"errc\":\"%s\",", errc_name(a.why));
    write_("RamImageDeclined", f, app::Event::RamImageDeclined::kKind, h);
}

void EventTrace::record(const app::Event::RequestDone& a, const app::Event::Head& h) noexcept {
    if (!admit_())
        return;
    char f[96];
    std::snprintf(f, sizeof f, "\"req\":\"%s\",", request_name(a.which));
    write_("RequestDone", f, app::Event::RequestDone::kKind, h);
}

void EventTrace::record_misrouted() noexcept {
    if (!admit_())
        return;
    log_.appendf(
        "{\"t\":\"ev\",\"k\":\"?\",\"depth\":%u,\"suppressed\":%u,\"seq\":%u}",
        static_cast<unsigned>(queue_->depth()),
        static_cast<unsigned>(suppressed_),
        static_cast<unsigned>(seq_)
    );
    ++seq_;
}

}
