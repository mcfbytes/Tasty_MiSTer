// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/ifup_starter.h"

#include "infra/path_text.h"

namespace mister::svc {
namespace {

constexpr char kIfup[] = "/sbin/ifup";
constexpr char kIfdown[] = "/sbin/ifdown";
constexpr char kPathEnv[] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";
constexpr char kHomeEnv[] = "HOME=/";
constexpr char kNullTty[] = "/dev/null";

[[nodiscard]] Ex<os::ChildProcess> spawn_route(const char* program, const os::IfName& ifname) {
    const char* argv[] = {program, ifname.c_str()};
    const char* envp[] = {kPathEnv, kHomeEnv};
    infra::PathText tty;
    if (!tty.assign(kNullTty))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});

    const os::ChildProcessSpec spec{.argv = argv, .envp = envp, .tty = tty};
    return os::ChildProcess::spawn(spec);
}

}

Ex<os::ChildProcess> IfupStarter::start(os::IfName ifname) const {
    return spawn_route(kIfup, ifname);
}

Ex<os::ChildProcess> IfupStarter::stop(os::IfName ifname) const {
    return spawn_route(kIfdown, ifname);
}

}
