// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/hotplug_watch.h"

#include <sys/inotify.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

#include "input_service_detail.h"

namespace mister::svc {

Ex<HotplugWatch> HotplugWatch::open(std::string_view dir) {
    UniqueFd inotify{::inotify_init1(IN_NONBLOCK | IN_CLOEXEC)};
    if (!inotify.valid())
        return std::unexpected(os_error(ERR_SITE()));
    const std::string path(dir);
    if (::inotify_add_watch(inotify.get(), path.c_str(), IN_MODIFY | IN_CREATE | IN_DELETE) < 0) {
        return std::unexpected(os_error(ERR_SITE()));
    }
    return HotplugWatch{std::move(inotify)};
}

Ex<void> HotplugWatch::on_ready() {

    alignas(struct inotify_event) char buf[4096];
    bool changed = false;
    while (true) {
        const ssize_t n = ::read(inotify_.get(), buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            return std::unexpected(os_error(ERR_SITE()));
        }
        if (n == 0)
            break;
        std::size_t off = 0;
        while (off + sizeof(struct inotify_event) <= static_cast<std::size_t>(n)) {
            struct inotify_event ev{};
            std::memcpy(&ev, buf + off, sizeof ev);
            if (ev.len != 0 && (ev.mask & (IN_CREATE | IN_DELETE)) != 0)
                changed = true;
            off += sizeof(struct inotify_event) + ev.len;
        }
    }

    if (changed)
        pending_ = true;
    return {};
}

}
