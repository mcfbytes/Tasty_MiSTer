// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <utility>

#include "hal/reg_block.h"
#include "hal/reg_port.h"
#include "hal/register_window.h"
#include "infra/seat.h"

namespace mister::hal {

template <class Layout, class Owner>
class WindowPort final : public IRegPort<Layout> {
    TASTY_SEAT_FROM(Owner);

public:
    explicit WindowPort(reg::Block lease) noexcept
        : regs_(RegisterWindow<Layout>::borrow(std::move(lease))) {}

    [[nodiscard]] RegWord read(typename Layout::Reg r) noexcept override {
        TASTY_SEAT_BODY(SeatOwner);
        return RegWord{regs_.read(r)};
    }

    void write(typename Layout::Reg r, RegWord w) noexcept override {
        TASTY_SEAT_BODY(SeatOwner);
        regs_.write(r, w);
    }

private:
    RegisterWindow<Layout> regs_;
};

}
