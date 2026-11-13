// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/host_led_watch.h"

#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <string>

namespace mister::app {

HostLedWatch::HostLedWatch(UniqueFd fd, xthread::WakeFlag& activity) noexcept
    : fd_(static_cast<UniqueFd&&>(fd)),
      activity_(&activity) {}

Ex<HostLedWatch> HostLedWatch::open(std::string_view node, xthread::WakeFlag& activity) noexcept {
    return open_at(kSysfsLeds, node, activity);
}

Ex<HostLedWatch> HostLedWatch::open_at(
    std::string_view prefix,
    std::string_view node,
    xthread::WakeFlag& activity
) noexcept {
    if (node.empty())
        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), 0});
    std::string path{prefix};
    path.append(node);
    path.append(kAttribute);
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
    }
    return HostLedWatch{UniqueFd{fd}, activity};
}

void HostLedWatch::on_ready() noexcept {
    char buf[16];
    const ssize_t n = ::pread(fd_.get(), buf, sizeof buf - 1, 0);
    if (n > 0 && buf[0] != '0')
        activity_->request();
}

}
