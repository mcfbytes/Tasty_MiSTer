// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/adv7513_breaker.h"

namespace mister::svc::adv7513 {

Ex<std::uint8_t> I2cBreaker::probe(unsigned bus, std::uint8_t addr) {
    TASTY_SEAT_BODY(I2cBreaker);
    return inner_->probe(bus, addr);
}

Ex<void> I2cBreaker::open(unsigned bus, std::uint8_t addr) {
    TASTY_SEAT_BODY(I2cBreaker);
    return inner_->open(bus, addr);
}

Ex<std::uint8_t> I2cBreaker::read(std::uint8_t addr, std::uint8_t reg) {
    TASTY_SEAT_BODY(I2cBreaker);
    if (open_) {
        ++refused_;
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), addr});
    }
    auto r = inner_->read(addr, reg);
    note(r.has_value(), addr, reg, r ? nullptr : &r.error());
    return r;
}

Ex<void> I2cBreaker::write(std::uint8_t addr, std::uint8_t reg, std::uint8_t val) {
    TASTY_SEAT_BODY(I2cBreaker);
    if (open_) {
        ++refused_;
        return std::unexpected(Error{Errc::would_block, ERR_SITE(), addr});
    }
    auto r = inner_->write(addr, reg, val);
    note(r.has_value(), static_cast<std::uint8_t>(addr | kOpWrite), reg, r ? nullptr : &r.error());
    return r;
}

void I2cBreaker::reset() noexcept {
    open_ = false;
    run_ = 0;
}

void I2cBreaker::note(bool ok, std::uint8_t op, std::uint8_t reg, const Error* e) noexcept {
    if (ok) {
        run_ = 0;
        return;
    }

    if (e == nullptr || e->code != Errc::io) return;
    ++failures_;
    const std::uint16_t err =
        e->detail > 0xFFFFu ? std::uint16_t{0xFFFF} : static_cast<std::uint16_t>(e->detail);
    last_errno_ = err;
    if (first_.op == 0) first_ = Failure{op, reg, err};
    if (++run_ >= kTripAfter && !open_) {
        open_ = true;
        ++trips_;
    }
}

}  // namespace mister::svc::adv7513
