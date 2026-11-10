// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/boot_arm.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <string>

#include "app/config_apply.h"
#include "app/session_machine.h"
#include "app/fenced_core_signals.h"
#include "app/xml_kind.h"

namespace mister::app {

bool BootArm::in_boot_state() const noexcept {
    TASTY_SEAT_BODY(BootArm);
    return machine_.state() == SessionState::Boot;
}

infra::OptRef<StdoutRouter> BootArm::boot_stdout_router() noexcept {
#ifndef NDEBUG

    if (!in_boot_state()) {
        fatal(
            Error{Errc::negotiation, ERR_SITE(), 0},
            "boot_stdout_router after the boot pump is sealed"
        );
    }
#endif
    return infra::opt_ref_of(stdout_armed_ ? &stdout_router_ : nullptr);
}

Ex<void> BootArm::route_stdout(std::uint8_t debug) {
    TASTY_SEAT_BODY(BootArm);
    return stdout_router().route(debug);
}

Ex<LoadRequest> BootArm::argv_request(const BootArgs& args) const {
    TASTY_SEAT_BODY(BootArm);

    LoadRequest req{};
    if (args.core[0] != '\0') {
        if (!req.path.assign(args.core))
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});
        req.kind = xml_kind_of(args.core);
    }
    if (args.companion) {
        if (!req.xml.assign(*args.companion))
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 2});
    }

    if (req.kind != XmlKind::Rbf) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(req.kind)}
        );
    }
    return req;
}

UiRequest::LoadCore BootArm::program(const LoadRequest& req) {
    TASTY_SEAT_BODY(BootArm);
    UiRequest::LoadCore sw{};
    sw.path = req.path;
    sw.xml = req.kind;

    signals_.set_core_reset(true);
    return sw;
}

void BootArm::hold_for_reboot() {
    TASTY_SEAT_BODY(BootArm);
    sync_and_hold_();
}

void BootArm::sync_and_hold_() {

    ::sync();
    signals_.set_core_reset(true);
}

ExitReboot BootArm::handoff_to_alternate_executable(const char* exe, const BootArgs& args) {
    TASTY_SEAT_BODY(BootArm);
    return handoff_exec_(exe, args);
}

Ex<void> BootArm::re_exec(const LoadRequest& req) {
    TASTY_SEAT_BODY(BootArm);
    return re_exec_(req);
}

ExitReboot BootArm::handoff_exec_(const char* exe, const BootArgs& args) {

    ::sync();
    signals_.set_core_reset(true);
    const char* core = args.core;
    const char* companion = args.companion.value_or(nullptr);

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

Ex<void> BootArm::re_exec_(const LoadRequest& req) {

    ::sync();
    signals_.set_core_reset(true);

    const std::string app = self_exe_path();
    if (app.empty()) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
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

    return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
}

}
