// SPDX-License-Identifier: GPL-3.0-or-later
#include "reactor/cause_decode.h"
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
        if (r >= 0 || errno != EINTR)
            return r;
    }
}

}

Ex<Notifier> Notifier::doorbell_adopt(
    os::UioLineRef uio,
    infra::OptRef<const hal::RegisterWindow<hal::CauseReg>> cause,
    Cause klass
) {

    if (uio.fd() < 0) {
        return std::unexpected(
            Error{Errc::uio_open, ERR_SITE(), static_cast<std::uint32_t>(EBADF)}
        );
    }

    (void)uio.consume();
    if (auto armed = uio.rearm(); !armed) {
        return std::unexpected(Error{Errc::uio_open, ERR_SITE(), armed.error().detail});
    }
    return Notifier{uio, cause, klass};
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

    const std::chrono::seconds whole = std::chrono::duration_cast<std::chrono::seconds>(period);
    const std::chrono::nanoseconds frac = period - whole;
    spec.it_interval.tv_sec = static_cast<time_t>(whole.count());
    spec.it_interval.tv_nsec = static_cast<long>(frac.count());
    spec.it_value = spec.it_interval;
    if (::timerfd_settime(fd, 0, &spec, nullptr) < 0) {
        const auto e = static_cast<std::uint32_t>(errno);
        ::close(fd);
        return std::unexpected(Error{Errc::os, ERR_SITE(), e});
    }
    return Notifier{Kind::Ticker, UniqueFd{fd}, Cause::Tick};
}

Ex<Notifier> Notifier::frame(int eventfd) {

    if (eventfd < 0) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EBADF)});
    }
    const int fd = ::dup(eventfd);
    if (fd < 0) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    return Notifier{Kind::Frame, UniqueFd{fd}, Cause::Frame};
}

Ex<Notifier> Notifier::test(int eventfd, Cause injected) {

    if (eventfd < 0) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(EBADF)});
    }
    const int fd = ::dup(eventfd);
    if (fd < 0) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    return Notifier{Kind::Test, UniqueFd{fd}, injected};
}

CauseDecode Notifier::drain_checked() {
    switch (kind_) {
        case Kind::Ticker: {

            std::uint64_t expirations = 0;
            const ssize_t n = read_exact(fd_.get(), &expirations, sizeof expirations);
            if (n != static_cast<ssize_t>(sizeof expirations))
                return CauseDecode::idle();
            return CauseDecode::of(Cause::Tick);
        }
        case Kind::Frame: {

            std::uint64_t edges = 0;
            const ssize_t n = read_exact(fd_.get(), &edges, sizeof edges);
            if (n != static_cast<ssize_t>(sizeof edges))
                return CauseDecode::idle();
            return CauseDecode::of(Cause::Frame);
        }
        case Kind::Test: {
            std::uint64_t v = 0;
            const ssize_t n = read_exact(fd_.get(), &v, sizeof v);
            if (n != static_cast<ssize_t>(sizeof v))
                return CauseDecode::idle();
            return CauseDecode::of(delivers_);
        }
        case Kind::Doorbell: {

            if (!uio_.consume())
                return CauseDecode::idle();
            const std::uint32_t word = cause_ ? cause_->read(hal::CauseReg::Reg::Cause) : 0u;
            const CauseDecode d = decode_cause_checked(word);

            (void)uio_.rearm();

            if (d.cause() != Cause::None && d.cause() != delivers_) {
                return CauseDecode::refused();
            }
            return d;
        }
    }
    return CauseDecode::idle();
}

}
