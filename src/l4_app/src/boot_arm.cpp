// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/boot_arm.h"

#include <fcntl.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <string>
#include <string_view>

#include "app/config_apply.h"
#include "app/session_machine.h"
#include "app/fenced_core_signals.h"

namespace mister::app {

namespace {

bool ieq(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (std::tolower(ca) != std::tolower(cb))
            return false;
    }
    return true;
}

XmlKind xml_kind_of(std::string_view s) {
    if (s.size() <= 4)
        return XmlKind::Rbf;
    const std::string_view tail = s.substr(s.size() - 4);
    if (ieq(tail, ".mra"))
        return XmlKind::Mra;
    if (ieq(tail, ".mgl"))
        return XmlKind::Mgl;
    return XmlKind::Rbf;
}

}

bool BootArm::in_boot_state() const noexcept {
    TASTY_SEAT_BODY(BootArm);
    return machine_.state() == SessionState::Boot;
}

StdoutRouter* BootArm::boot_stdout_router() noexcept {
#ifndef NDEBUG

    if (!in_boot_state()) {
        fatal(
            Error{Errc::negotiation, ERR_SITE(), 0},
            "boot_stdout_router after the boot pump is sealed"
        );
    }
#endif
    return stdout_armed_ ? &stdout_router_ : nullptr;
}

Ex<void> BootArm::route_stdout(std::uint8_t debug) {
    TASTY_SEAT_BODY(BootArm);
    return stdout_router().route(debug);
}

Ex<LoadRequest> BootArm::argv_request(std::span<const char* const> argv) const {
    TASTY_SEAT_BODY(BootArm);

    const char* rbf = (argv.size() > 1 && argv[1] != nullptr) ? argv[1] : "";
    const char* xml = (argv.size() > 2) ? argv[2] : nullptr;
    if (xml != nullptr && xml[0] == '\0')
        xml = nullptr;

    LoadRequest req{};
    if (rbf[0] != '\0') {
        if (!req.path.assign(rbf))
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});
        req.kind = xml_kind_of(rbf);
    }
    if (xml != nullptr) {
        if (!req.xml.assign(xml))
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

ExitReboot BootArm::handoff_to_alternate_executable(
    const char* exe,
    std::span<const char* const> argv
) {
    TASTY_SEAT_BODY(BootArm);
    return handoff_exec_(exe, argv);
}

Ex<void> BootArm::re_exec(const LoadRequest& req) {
    TASTY_SEAT_BODY(BootArm);
    return re_exec_(req);
}

ExitReboot BootArm::handoff_exec_(const char* exe, std::span<const char* const> argv) {

    ::sync();
    signals_.set_core_reset(true);
    const char* path = (argv.size() > 1 && argv[1] != nullptr) ? argv[1] : "";
    const char* xml = (argv.size() > 2) ? argv[2] : nullptr;
    if (xml != nullptr && xml[0] == '\0')
        xml = nullptr;

    const ::pid_t child = ::fork();
    if (child > 0)
        ::_exit(0);
    if (child == 0) {
        (void)::setsid();
        const ::pid_t grandchild = ::fork();
        if (grandchild > 0)
            ::_exit(0);
        if (grandchild == 0) {

            (void)::execl(exe, exe, path, xml, static_cast<char*>(nullptr));
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
