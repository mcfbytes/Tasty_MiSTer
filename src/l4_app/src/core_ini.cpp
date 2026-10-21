// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/core_ini.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <string>
#include <utility>

#include "app/config_apply.h"
#include "cores/registry.h"

namespace mister::app {
namespace {

bool mount_present(std::string_view needle) {
    if (needle.empty())
        return true;
    const int fd = ::open("/proc/mounts", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    std::string text;
    char buf[4096];
    for (;;) {
        const ::ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (n == 0)
            break;
        text.append(buf, static_cast<std::size_t>(n));
        if (text.size() > (1u << 20))
            break;
    }
    (void)::close(fd);
    return text.find(needle) != std::string::npos;
}

}

void CoreIni::arm_replay(bool strict) noexcept {
    replay_ = true;
    strict_ = strict;
}

CoreIni::Resolved CoreIni::resolve(std::string_view conf_name, const MraFacts& facts) {
    TASTY_SEAT_BODY(CoreIni);
    Resolved out;
    out.cfg = parse_ini_for_core(&vfs_, svc::ConfigSnapshot{}, conf_name, facts);
    svc::ConfigSnapshot& cfg = out.cfg;
    if (replay_) {

        const std::uint8_t dv = svc::direct_video_resolved(cfg);
        dv_cell_.publish(dv);
        cfg.waitmount[0] = '\0';
        if (!strict_ && dv != 0)
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
    boot_pending_ = false;
}

bool CoreIni::wait_for_mount_(std::string_view needle) {
    constexpr unsigned kPollMs = 1000;
    constexpr unsigned kBudgetMs = 60'000;
    for (unsigned waited = 0;; waited += kPollMs) {
        if (mount_present(needle))
            return true;
        if (waited >= kBudgetMs)
            return false;
        if (sleeper_) {
            sleeper_->sleep_ms(kPollMs);
        } else {
            ::usleep(static_cast<::useconds_t>(kPollMs) * 1000u);
        }
    }
}

}
