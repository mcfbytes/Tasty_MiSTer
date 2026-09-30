// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/process_integration.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <array>

#include "infra/unique_fd.h"

#include "os/bt_probe.h"
#include "os/run_sync.h"
#include "hal/thread_map.h"

namespace mister::app::proc {

Ex<void> uartmode_script(std::uint8_t mode, int* exit_code) {

    char n[8] = {};
    const int len = std::snprintf(n, sizeof n, "%u", static_cast<unsigned>(mode));
    if (len <= 0) return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    const std::array<const char*, 2> argv{"uartmode", n};
    auto rc = os::run_sync(argv);
    if (!rc) return std::unexpected(rc.error());

    if (*rc == 127) {
        return std::unexpected(Error{Errc::not_found, ERR_SITE(), 127});
    }
    if (exit_code != nullptr) *exit_code = *rc;
    return {};
}

Ex<void> mlinkutil_baud(std::uint32_t) { return unimplemented(ERR_SITE()); }

Ex<void> mlinkutil_soundfont(std::string_view) { return unimplemented(ERR_SITE()); }

Ex<std::string> midilink_soundfont() { return unimplemented(ERR_SITE()); }

Ex<void> bluetoothd_hcireset() {

    const std::array<const char*, 2> argv{"/bin/bluetoothd", "hcireset"};
    return os::run_detached(argv);
}

Ex<void> bluetoothd_renew() {

    const std::array<const char*, 2> argv{"/bin/bluetoothd", "renew"};
    auto rc = os::run_sync(argv);
    if (!rc) return std::unexpected(rc.error());
    if (*rc == 127) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 127});
    return {};
}

Ex<void> hciconfig_reset() {

    const std::array<const char*, 3> argv{"hciconfig", "hci0", "reset"};
    auto rc = os::run_sync(argv);
    if (!rc) return std::unexpected(rc.error());
    if (*rc == 127) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 127});
    return {};
}

Ex<void> btctl_disconnect(std::string_view) { return unimplemented(ERR_SITE()); }

Ex<void> killall_sigint_bt() {

    const std::array<const char*, 4> argv{"killall", "-SIGINT", "btpair", "btctl"};
    auto rc = os::run_sync(argv);
    if (!rc) return std::unexpected(rc.error());
    if (*rc == 127) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 127});
    return {};
}

[[nodiscard]] Ex<bool> bt_adapter_up() { return os::bluetooth_adapter_up(); }

[[nodiscard]] Ex<void> killall(const char* name) {

    const std::array<const char*, 2> argv{"killall", name};
    auto rc = os::run_sync(argv);
    if (!rc) return std::unexpected(rc.error());
    if (*rc == 127) return std::unexpected(Error{Errc::not_found, ERR_SITE(), 127});
    return {};
}

void widen_ui_affinity() noexcept {
    if (!hal::widen_self_affinity())
        std::fprintf(stderr, "mister: T-UI affinity widen not applied\n");
}

void restore_ui_affinity() noexcept {
    if (!hal::restore_self_affinity())
        std::fprintf(stderr, "mister: T-UI affinity narrow not applied\n");
}

Ex<void> write_tmp_handoff(const char* path, std::string_view data) {

    const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    UniqueFd guard{fd};
    std::size_t off = 0;
    while (off < data.size()) {
        const ::ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
        }
        if (n == 0) break;
        off += static_cast<std::size_t>(n);
    }
    return {};
}

}  // namespace mister::app::proc
