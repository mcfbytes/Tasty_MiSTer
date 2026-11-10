// SPDX-License-Identifier: GPL-3.0-or-later
#include "host_facts.h"

#include <sys/statvfs.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string_view>

#include "infra/seat.h"
#include "os/bt_probe.h"
#include "os/clock.h"
#include "os/net_probe.h"
#include "proto/types.h"
#include "svc/vfs.h"
#include "ui/menu_model.h"
#include "ui/status_bar_facts.h"
#include "ui/system_facts.h"

namespace mister::fw {

ui::HostSample HostFacts::snapshot() {
    constexpr std::chrono::seconds kProbeInterval{3};
    TASTY_SEAT_BODY(HostFacts);

    ui::HostSample out{};
    ui::SystemFacts& sys = out.sys;
    ui::StatusBarFacts& bar = out.bar;

    static_assert(
        sizeof(TASTY_VDATE) == 7,
        "video-rule: TASTY_VDATE is YYMMDD — six chars plus the NUL, the "
        "exact width menu.cpp renders"
    );
    (void)sys.version.assign(std::string_view{TASTY_VDATE});
    sys.kernel = kernel_rt_;

    constexpr std::size_t kOsVersionBytes = 6;
    if (FILE* f = std::fopen("/MiSTer.version", "r")) {
        char raw[kOsVersionBytes + 1] = {};
        if (std::fread(raw, kOsVersionBytes, 1, f) == 1) {
            (void)sys.os_version.assign(std::string_view{raw});
        }
        (void)std::fclose(f);
    }

    struct statvfs st{};
    if (::statvfs(vfs_.root_path().c_str(), &st) == 0) {
        avail_ = proto::FileSize{
            static_cast<std::uint64_t>(st.f_bsize) * static_cast<std::uint64_t>(st.f_bavail)
        };
    }

    sys.avail = avail_;
    sys.storage_usb = vfs_.root().on_usb();

    const std::time_t now = std::time(nullptr);
    struct tm tmv{};
    if (::localtime_r(&now, &tmv) != nullptr) {
        char text[decltype(bar.clock)::kBufSize] = {};
        (void)std::strftime(text, sizeof text, "%b %d %a%H:%M:%S", &tmv);
        (void)bar.clock.assign(std::string_view{text});
    }
    bar.splash = !splash_spent_;
    splash_spent_ = true;

    const bool pairing = menu_.bt_pairing_active();
    if (!pairing) {
        const auto mono = clock_.now();
        if (!bt_probed_ || mono - bt_probed_at_ >= kProbeInterval) {
            bt_probed_ = true;
            bt_probed_at_ = mono;
            if (auto up = os::bluetooth_adapter_up())
                bt_up_ = *up;
        }
    }
    bar.bluetooth = bt_up_;

    const auto mono = clock_.now();
    if (!net_probed_ || mono - net_probed_at_ >= kProbeInterval) {
        net_probed_ = true;
        net_probed_at_ = mono;
        if (auto np = os::net_presence()) {
            net_wired_ = np->wired;
            net_wifi_ = np->wifi;
        }
    }
    bar.net_wired = net_wired_;
    bar.net_wifi = net_wifi_;
    return out;
}

}
