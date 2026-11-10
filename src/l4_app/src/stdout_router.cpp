// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/stdout_router.h"

#include "infra/opt_ref.h"
#include "infra/posix_compat.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

namespace mister::app {

StdoutRouter::~StdoutRouter() {

    if (orig_)
        infra::set_stdout_file(&*orig_);
}

Ex<void> StdoutRouter::install_initial_silence() {

    if (!orig_)
        orig_ = infra::opt_ref_of(stdout);
    if (!null_) {
        std::FILE* f = std::fopen(paths_.null_sink, "w");
        if (f == nullptr) {

            return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
        }
        const int fd = ::fileno(f);
        if (fd >= 0)
            (void)::fcntl(fd, F_SETFD, FD_CLOEXEC);
        null_.reset(f);
        infra::set_stdout_file(f);
        sink_ = Sink::Null;
    }

    if (!dbg_) {
        std::FILE* f = std::fopen(paths_.debug_file, "w");
        if (f != nullptr) {
            (void)std::setvbuf(f, nullptr, _IONBF, 0);
            const int fd = ::fileno(f);
            if (fd >= 0)
                (void)::fcntl(fd, F_SETFD, FD_CLOEXEC);
            dbg_.reset(f);
        }
    }
    return {};
}

Ex<void> StdoutRouter::route(std::uint8_t debug) {
    if (!orig_)
        orig_ = infra::opt_ref_of(stdout);
    const Sink target = sink_for(debug);

    infra::OptRef<std::FILE> to{};
    switch (target) {
        case Sink::Null:
            to = infra::opt_ref_of(null_.get());
            break;
        case Sink::Original:
            to = orig_;
            break;
        case Sink::DebugFile:
            to = infra::opt_ref_of(dbg_.get());
            break;
    }

    if (!to) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(target)});
    }
    infra::set_stdout_file(&*to);
    sink_ = target;
    return {};
}

}
