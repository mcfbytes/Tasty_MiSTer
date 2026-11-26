// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/boot_handoff.h"

#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <string>

#include "app/config_apply.h"
#include "infra/prelude.h"

namespace mister::app {

void BootHandoff::sync_() noexcept {
    ::sync();
}

void BootHandoff::pause_() noexcept {
    sleep_ms(sleeper_, kSettlePause);
}

ExitReboot BootHandoff::exec_alternate_(const char* exe, const BootArgs& args) {

    const char* core = args.core;
    const char* companion = args.companion.value_or(nullptr);

    std::fprintf(
        stderr,
        "mister: current exec is %s, core requires exec %s: handing off\n",
        self_exe_path().c_str(),
        exe
    );

    const ::pid_t child = ::fork();
    if (child > 0)
        ::_exit(0);
    if (child == 0) {
        (void)::setsid();
        const ::pid_t grandchild = ::fork();
        if (grandchild > 0)
            ::_exit(0);
        if (grandchild == 0) {

            (void)::execl(exe, exe, core, companion, static_cast<char*>(nullptr));
        }
    }

    return ExitReboot::Cold;
}

Ex<void> BootHandoff::re_exec_(const LoadRequest& req) {
    const std::string app = self_exe_path();
    if (app.empty()) {
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }
    const std::string path(req.path.view());
    const std::string xml(req.xml.view());

    const char* xml_arg = xml.empty() ? nullptr : xml.c_str();

    const ::pid_t child = ::fork();
    if (child > 0)
        ::_exit(0);
    if (child == 0) {
        (void)::setsid();
        const ::pid_t grandchild = ::fork();
        if (grandchild > 0)
            ::_exit(0);
        if (grandchild == 0) {
            (void)::execl(
                app.c_str(),
                app.c_str(),
                path.c_str(),
                xml_arg,
                static_cast<char*>(nullptr)
            );
        }
    }

    return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
}

}
