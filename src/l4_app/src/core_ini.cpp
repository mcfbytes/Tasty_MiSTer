// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/core_ini.h"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "core_ini_mounts.h"
#include "os/small_file.h"

#include "app/board_ops.h"
#include "app/config_apply.h"
#include "cores/registry.h"
#include "hal/boot_handoff.h"
#include "svc/scan_filter.h"
#include "svc/vfs.h"

namespace mister::app {
namespace detail {

bool mount_present(std::string_view needle, std::string_view path) {
    if (needle.empty())
        return true;
    std::array<char, kProcMountsTextMax> text{};
    const std::string target{path};
    const auto n = os::read_small_file(target.c_str(), std::span<char>(text));
    if (!n)
        return false;
    return std::string_view(n->data(), n->size()).find(needle) != std::string_view::npos;
}

}

void CoreIni::arm_replay(bool strict) noexcept {
    replay_ = true;
    strict_ = strict;
}

CoreIni::Resolved CoreIni::resolve(std::string_view conf_name, const MraFacts& facts) {
    TASTY_SEAT_BODY(CoreIni);
    Resolved out;

    svc::IniCatalog cat;
    if (auto entries = vfs_.scan(
            "",
            svc::ScanFilter{.extensions = "INI", .directories = false, .files = true}
        )) {
        cat.index = svc::AltIniIndex::from_scan(*entries);
    }
    std::uint8_t alt = 0;
    if (handoff_) {
        if (const auto page = handoff_->handoff())
            alt = page->altcfg.value_or(0);
    }
    cat.active = svc::ini_choice_of_handoff(alt);
    ini_cell_.publish(cat);
    out.cfg = parse_ini_for_core(&vfs_, svc::ConfigSnapshot{}, conf_name, facts, cat);
    svc::ConfigSnapshot& cfg = out.cfg;
    if (replay_) {

        const svc::DirectVideo dv = svc::direct_video_resolved(cfg);
        dv_cell_.publish(svc::wire_of(dv));
        cfg.waitmount[0] = '\0';
        if (!strict_ && dv != svc::DirectVideo::Off)
            cfg.direct_video = 0;
    }
    if (!boot_pending_)
        return out;

    if (cfg.waitmount[0] != '\0' && !cores::profile_for(conf_name).is_front_end) {
        if (!wait_for_mount_(cfg.waitmount))
            out.waitmount_exhausted = true;
    }
    boot_handoff_.clear();
    if (const LauncherProfile* l = launcher_for_main(cfg.main)) {
        out.launcher = infra::OptRef<const LauncherProfile>{*l};
        return out;
    }
    if (auto exe = alternate_executable(vfs_, cfg.main, self_exe_path())) {
        boot_handoff_ = std::move(*exe);
    }
    return out;
}

void CoreIni::publish(const svc::ConfigSnapshot& cfg) {
    TASTY_SEAT_BODY(CoreIni);
    cell_.publish(cfg);
    idle_cell_.publish(idle_knobs_of(cfg));
    boot_pending_ = false;
}

bool CoreIni::wait_for_mount_(std::string_view needle) {
    constexpr unsigned kPollMs = 1000;
    constexpr unsigned kBudgetMs = 60'000;
    for (unsigned waited = 0;; waited += kPollMs) {
        if (detail::mount_present(needle))
            return true;
        if (waited >= kBudgetMs)
            return false;
        sleep_ms(sleeper_, std::chrono::milliseconds{kPollMs});
    }
}

}
