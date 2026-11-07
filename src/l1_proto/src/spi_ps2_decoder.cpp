// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/spi_ps2_decoder.h"

#include "hal/selected.h"
#include "infra/endian.h"
#include "proto/ps2_wire.h"

#include <span>
#include "proto/uio_opcodes.h"

namespace mister::proto {

namespace {

constexpr std::size_t kPairBytes = 2;
}

[[nodiscard]] Ex<void> write_ps2_frame(ILinkFrame& frame, const Ps2Frame& f) {
    if (f.len == 0 || f.len > kPs2FrameBytes) {
        return std::unexpected(Error{Errc::bad_opcode, ERR_SITE(), f.len});
    }
    const bool mouse = f.stream == Ps2Stream::MousePacket || f.stream == Ps2Stream::MouseReply;
    const bool reply = f.stream == Ps2Stream::KeyboardReply || f.stream == Ps2Stream::MouseReply;
    if (f.stream == Ps2Stream::MousePacket && (f.len % 2u) != 0u) {
        return std::unexpected(Error{Errc::bad_opcode, ERR_SITE(), f.len});
    }
    std::uint16_t words[kPs2FrameBytes];
    std::size_t n = 0;
    if (f.stream == Ps2Stream::MousePacket) {
        for (std::uint8_t i = 0; i + 1u < f.len; i = static_cast<std::uint8_t>(i + 2u)) {
            words[n++] = infra::load_le<std::uint16_t>(
                std::span<const std::uint8_t, kPairBytes>(f.bytes.data() + i, kPairBytes)
            );
        }
    } else {
        for (std::uint8_t i = 0; i < f.len; ++i) {
            words[n++] = static_cast<std::uint16_t>(reply ? (0xFF00u | f.bytes[i]) : f.bytes[i]);
        }
    }
    return send_frame(
        frame,
        mouse ? UioOp::Mouse : UioOp::Keyboard,
        std::span<const std::uint16_t>(words, n)
    );
}

void SpiPs2Decoder::probe() noexcept {
    if (ps2_control_)
        return;
    if (led_poll_div_ != 0) {
        --led_poll_div_;
        return;
    }
    led_poll_div_ = kLedPollTicks - 1;
    hal::Selected cs(*link_, hal::ChipSelect::Io);
    if (auto r = link_->transfer(word(UioOp::GetKbdLed)); !r)
        return count_error_();
    auto w = link_->transfer(hal::SpiWord{0});
    if (!w)
        return count_error_();
    if ((w->v & 0x100u) != 0u)
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

    hal::Selected cs(*link_, hal::ChipSelect::Io);
    if (auto r = link_->transfer(word(UioOp::Ps2Ctl)); !r)
        return count_error_();
    auto w0 = link_->transfer(hal::SpiWord{0});
    if (!w0)
        return count_error_();
    auto w1 = link_->transfer(hal::SpiWord{0});
    if (!w1)
        return count_error_();

    if (!out_.push(LinkEvent::Ps2Control{.keyboard = w0->v, .mouse = w1->v}))
        count_error_();
}
}
