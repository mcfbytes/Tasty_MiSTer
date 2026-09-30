// SPDX-License-Identifier: GPL-3.0-or-later
#include "reactor/notifier.h"

#include <sys/timerfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <ctime>

namespace mister::reactor {

namespace {

ssize_t read_exact(int fd, void* buf, std::size_t n) {
    for (;;) {
        const ssize_t r = ::read(fd, buf, n);
        if (r >= 0 || errno != EINTR) return r;
    }
}

}  // namespace

Ex<Notifier> Notifier::doorbell_adopt(os::UioHandle uio,
                                      const hal::RegisterWindow<hal::CauseReg>* cause,
                                      Cause klass) {

    if (uio.fd() < 0) {
        return std::unexpected(
            Error{Errc::uio_open, ERR_SITE(), static_cast<std::uint32_t>(EBADF)});
    }
    return Notifier{static_cast<os::UioHandle&&>(uio), cause, klass};
}

Ex<Notifier> Notifier::ticker(std::chrono::nanoseconds period) {

    if (period <= std::chrono::nanoseconds::zero()) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), 0});
    }
    int fd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    itimerspec spec{};
    spec.it_interval.tv_sec = static_cast<time_t>(period.count() / 1'000'000'000);
    spec.it_interval.tv_nsec = static_cast<long>(period.count() % 1'000'000'000);
    spec.it_value = spec.it_interval;
    if (::timerfd_settime(fd, 0, &spec, nullptr) < 0) {
        const auto e = static_cast<std::uint32_t>(errno);
        ::close(fd);
        return std::unexpected(Error{Errc::os, ERR_SITE(), e});
    }
    return Notifier{Kind::Ticker, UniqueFd{fd}, nullptr, Cause::Tick};
}

Ex<Notifier> Notifier::frame(int eventfd) {

    if (eventfd < 0) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EBADF)});
    }
    const int fd = ::dup(eventfd);
    if (fd < 0) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    return Notifier{Kind::Frame, UniqueFd{fd}, nullptr, Cause::Frame};
}

Ex<Notifier> Notifier::test(int eventfd, Cause injected) {

    if (eventfd < 0) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EBADF)});
    }
    const int fd = ::dup(eventfd);
    if (fd < 0) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    return Notifier{Kind::Test, UniqueFd{fd}, nullptr, injected};
}

CauseDecode Notifier::drain_checked() {
    switch (kind_) {
        case Kind::Ticker: {

            std::uint64_t expirations = 0;
            const ssize_t n = read_exact(fd_.get(), &expirations, sizeof expirations);
            if (n != static_cast<ssize_t>(sizeof expirations)) return {};
            return {Cause::Tick, false};
        }
        case Kind::Frame: {

            std::uint64_t edges = 0;
            const ssize_t n = read_exact(fd_.get(), &edges, sizeof edges);
            if (n != static_cast<ssize_t>(sizeof edges)) return {};
            return {Cause::Frame, false};
        }
        case Kind::Test: {
            std::uint64_t v = 0;
            const ssize_t n = read_exact(fd_.get(), &v, sizeof v);
            if (n != static_cast<ssize_t>(sizeof v)) return {};
            return {delivers_, false};
        }
        case Kind::Doorbell: {

            if (!uio_.consume()) return {};
            const std::uint32_t word =
                cause_ != nullptr ? cause_->read(hal::CauseReg::Reg::Cause) : 0u;
            const CauseDecode d = decode_cause_checked(word);

            (void)uio_.rearm();

            if (d.cause != Cause::None && d.cause != delivers_) {
                return {Cause::None, true};
            }
            return d;
        }
    }
    return {};
}

}  // namespace mister::reactor
