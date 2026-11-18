// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_ps2_decoder.h"

#include "hal/selected.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

namespace {
constexpr hal::SpiWord kPs2ControlPresent{0x100};
}

void SpiPs2Decoder::probe() noexcept {
    if (ps2_control_)
        return;
    if (led_poll_div_ != 0) {
        --led_poll_div_;
        return;
    }
    led_poll_div_ = kLedPollTicks - 1;
    hal::Selected cs(link_, hal::ChipSelect::Io);
    if (auto r = link_.transfer(word(UioOp::GetKbdLed)); !r)
        return count_error_();
    auto w = link_.transfer(hal::SpiWord{0});
    if (!w)
        return count_error_();
    if (w->any_of(kPs2ControlPresent))
        ps2_control_ = true;
}

void SpiPs2Decoder::forget() noexcept {
    ps2_control_ = false;
    led_poll_div_ = 0;
    if (!out_.push(LinkEvent::Ps2ControlEnded{}))
        count_error_();
}

void SpiPs2Decoder::service() noexcept {
    if (!ps2_control_)
        return;

    hal::Selected cs(link_, hal::ChipSelect::Io);
    if (auto r = link_.transfer(word(UioOp::Ps2Ctl)); !r)
        return count_error_();
    auto w0 = link_.transfer(hal::SpiWord{0});
    if (!w0)
        return count_error_();
    auto w1 = link_.transfer(hal::SpiWord{0});
    if (!w1)
        return count_error_();

    if (!out_.push(
            LinkEvent::Ps2Control{.keyboard = Ps2ControlWord{*w0}, .mouse = Ps2ControlWord{*w1}}
        ))
        count_error_();
}
}
