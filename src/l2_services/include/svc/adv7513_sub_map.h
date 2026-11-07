// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/types.h"
#include "svc/adv7513_reg_map.h"
#include "svc/i2c_adapter.h"

namespace mister::svc::adv7513 {

class SubMap final : public IRegMap {
    TASTY_SEAT_RESIDENT(Ui);

public:
    SubMap() = default;

    SubMap(II2cAdapter& a, os::I2cAddr addr) noexcept : a_(a), addr_(addr) {}

    [[nodiscard]] Ex<std::uint8_t> read(os::I2cReg reg) override {
        TASTY_SEAT_BODY(SubMap);
        if (!a_) {
            return std::unexpected(Error{Errc::negotiation, ERR_SITE(), addr_detail(addr_)});
        }
        return a_->read(addr_, reg);
    }

    [[nodiscard]] Ex<void> write(os::I2cReg reg, std::uint8_t val) override {
        TASTY_SEAT_BODY(SubMap);
        if (!a_) {
            return std::unexpected(Error{Errc::negotiation, ERR_SITE(), addr_detail(addr_)});
        }
        return a_->write(addr_, reg, val);
    }

    os::I2cAddr address() const noexcept { return addr_; }

private:
    infra::OptRef<II2cAdapter> a_{};
    os::I2cAddr addr_{};
};

}
