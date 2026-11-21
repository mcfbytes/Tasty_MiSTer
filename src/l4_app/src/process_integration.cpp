// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/process_integration.h"

#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <span>

#include "infra/file_mode.h"
#include "infra/prelude.h"
#include "os/file_create.h"
#include "infra/unique_fd.h"
#include "hal/thread_map.h"
#include "os/bt_probe.h"
#include "os/run_sync.h"
#include "os/write_all.h"

namespace mister::app::proc {

namespace {

constexpr os::ChildProcessExit::ExitStatus kExecFailed{127};

}

[[nodiscard]] Ex<os::ChildProcessExit> uartmode_script(std::uint8_t mode) {

    char n[8] = {};
    const int len = std::snprintf(n, sizeof n, "%u", static_cast<unsigned>(mode));
    if (len <= 0)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    const std::array<const char*, 2> argv{"uartmode", n};
    auto rc = os::run_sync(argv);
    if (!rc)
        return std::unexpected(rc.error());

    if (rc->exited_with(os::ChildProcessExit::ExitStatus{kExecFailed})) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 127});
    }
    if (rc->reap_failed())
        return std::unexpected(Error{Errc::os, ERR_SITE(), 0});
    return *rc;
}

[[nodiscard]] Ex<void> mlinkutil_baud(cores::BaudRate) {

    return unimplemented(ERR_SITE());
}

[[nodiscard]] Ex<void> mlinkutil_soundfont(std::string_view) {

    return unimplemented(ERR_SITE());
}

[[nodiscard]] Ex<std::string> midilink_soundfont() {

    return unimplemented(ERR_SITE());
}

[[nodiscard]] Ex<void> btctl_disconnect(std::string_view) {

    return unimplemented(ERR_SITE());
}

[[nodiscard]] Ex<bool> bt_adapter_up() {

    return os::bluetooth_adapter_up();
}

[[nodiscard]] Ex<void> killall(const char* name) {

    const std::array<const char*, 2> argv{"killall", name};
    auto rc = os::run_sync(argv);
    if (!rc)
        return std::unexpected(rc.error());
    if (rc->exited_with(kExecFailed))
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 127});
    return {};
}

void widen_ui_affinity() noexcept {
    if (const auto w = hal::widen_self_affinity(); !w)
        std::fprintf(
            stderr,
            "mister: T-UI affinity widen not applied (Errc=%u detail=%u)\n",
            static_cast<unsigned>(w.error().code),
            w.error().detail
        );
}

void restore_ui_affinity() noexcept {
    if (const auto n = hal::restore_self_affinity(); !n)
        std::fprintf(
            stderr,
            "mister: T-UI affinity narrow not applied (Errc=%u detail=%u)\n",
            static_cast<unsigned>(n.error().code),
            n.error().detail
        );
}

[[nodiscard]] Ex<void> write_tmp_handoff(const char* path, std::string_view data) {

    constexpr os::OpenFlags kHandoffFlags = os::OpenFlags::write_only().creating().truncating();
    static_assert(kHandoffFlags.open_word() == (O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC));
    auto fd = os::open_created(path, kHandoffFlags, infra::kSharedFileMode);
    if (!fd)
        return std::unexpected(fd.error().refusal(Errc::os, ERR_SITE()));
    os::write_all_best_effort(*fd, std::as_bytes(std::span(data)));
    return {};
}

}
