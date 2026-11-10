// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/adv7513_breaker.h"

namespace mister::svc::adv7513 {

Ex<std::uint8_t> I2cBreaker::probe(unsigned bus, os::I2cAddr addr) {
    TASTY_SEAT_BODY(I2cBreaker);
    return inner_.probe(bus, addr);
}

Ex<void> I2cBreaker::open(unsigned bus, os::I2cAddr addr) {
    TASTY_SEAT_BODY(I2cBreaker);
    return inner_.open(bus, addr);
}

Ex<std::uint8_t> I2cBreaker::read(os::I2cAddr addr, os::I2cReg reg) {
    TASTY_SEAT_BODY(I2cBreaker);
    if (open_) {
        ++refused_;
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), addr_detail(addr)});
    }
    auto r = inner_.read(addr, reg);
    note(r.has_value(), addr, false, reg, r ? nullptr : &r.error());
    return r;
}

Ex<void> I2cBreaker::write(os::I2cAddr addr, os::I2cReg reg, std::uint8_t val) {
    TASTY_SEAT_BODY(I2cBreaker);
    if (open_) {
        ++refused_;
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), addr_detail(addr)});
    }
    auto r = inner_.write(addr, reg, val);
    note(r.has_value(), addr, true, reg, r ? nullptr : &r.error());
    return r;
}

void I2cBreaker::reset() noexcept {
    open_ = false;
    run_ = 0;
}

void I2cBreaker::note(
    bool ok,
    os::I2cAddr addr,
    bool write,
    os::I2cReg reg,
    const Error* e
) noexcept {
    if (ok) {
        run_ = 0;
        return;
    }

    if (e == nullptr || e->code != Errc::io)
        return;
    ++failures_;
    const std::uint16_t detail = e->detail > 0xFFFFu ? std::uint16_t{0xFFFF}
                                                     : static_cast<std::uint16_t>(e->detail);
    const OsErrno err{detail};
    last_errno_ = err;
    if (!first_)
        first_ = Failure{addr, write, reg, err};
    if (++run_ >= kTripAfter && !open_) {
        open_ = true;
        ++trips_;
    }
}

}
