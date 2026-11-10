// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/cmd_fifo.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>

#include "app/path_text.h"

namespace mister::app {

static_assert(kCmdLineMax <= app::kPathMax, "a FIFO read's operand always fits PathText");

CmdFifo::CmdFifo(UniqueFd fd, std::string_view path, ICmdVerbSink& route) noexcept
    : fd_(static_cast<UniqueFd&&>(fd)),
      route_(route) {

    (void)path_.assign(path);
}

CmdFifo::CmdFifo(CmdFifo&& o) noexcept
    : fd_(static_cast<UniqueFd&&>(o.fd_)),
      path_(o.path_),
      last_bad_(o.last_bad_),
      stats_(o.stats_),
      route_(o.route_),
      routed_(o.routed_) {

    o.path_.clear();
    o.last_bad_.clear();
}

CmdFifo& CmdFifo::operator=(CmdFifo&& o) noexcept {
    if (this != &o) {
        if (fd_.valid() && !path_.empty())
            (void)::unlink(path_.c_str());
        fd_ = static_cast<UniqueFd&&>(o.fd_);
        path_ = o.path_;
        last_bad_ = o.last_bad_;
        stats_ = o.stats_;
        route_ = o.route_;
        routed_ = o.routed_;
        o.path_.clear();
        o.last_bad_.clear();
    }
    return *this;
}

CmdFifo::~CmdFifo() {

    if (fd_.valid() && !path_.empty())
        (void)::unlink(path_.c_str());
}

std::string_view CmdFifo::path() const noexcept {
    return path_.view();
}

std::string_view CmdFifo::last_unrecognised() const noexcept {
    return last_bad_.view();
}

Ex<CmdFifo> CmdFifo::open(const char* path, ICmdVerbSink& route) {
    constexpr ::mode_t kFifoMode = 0666;
    if (path == nullptr) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    const std::string_view p{path};
    if (p.empty() || p.size() >= kPathMax) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(p.size())}
        );
    }

    if (::unlink(path) != 0 && errno != ENOENT) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }

    if (::mkfifo(path, kFifoMode) != 0) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }

    const int fd = ::open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        const auto e = static_cast<std::uint32_t>(errno);
        (void)::unlink(path);
        return std::unexpected(Error{Errc::os, ERR_SITE(), e});
    }
    return CmdFifo{UniqueFd{fd}, p, route};
}

Ex<unsigned> CmdFifo::service() {

    struct PublishOnExit {
        CmdFifo* self;

        ~PublishOnExit() { self->cell_.publish(self->stats_); }
    } publish_on_exit{this};

    if (!fd_.valid()) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), EBADF});
    }

    char buf[kCmdLineMax];
    const ssize_t len = ::read(fd_.get(), buf, sizeof(buf) - 1);

    if (len < 0) {
        const int e = errno;

        if (e == EAGAIN || e == EWOULDBLOCK || e == EINTR)
            return 0u;
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(e)});
    }
    if (len == 0)
        return 0u;
    ++stats_.reads;

    std::string_view rest{buf, static_cast<std::size_t>(len)};
    unsigned routed = 0;

    while (!rest.empty()) {
        const std::size_t nl = rest.find('\n');
        const std::string_view line = (nl == std::string_view::npos) ? rest : rest.substr(0, nl);
        rest = (nl == std::string_view::npos) ? std::string_view{} : rest.substr(nl + 1);

        if (line.empty())
            continue;
        ++stats_.lines;

        switch (deliver_cmd_line(line, route_.get())) {
            case CmdLineOutcome::Routed:
                ++routed_;
                ++routed;
                break;
            case CmdLineOutcome::Unrouted:
                ++stats_.unrouted;
                break;
            case CmdLineOutcome::Unrecognised:

                ++stats_.unrecognised;
                (void)last_bad_.assign(line);
                break;
        }
    }
    return routed;
}

}
