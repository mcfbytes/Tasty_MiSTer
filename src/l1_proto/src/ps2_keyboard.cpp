// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/ps2_keyboard.h"

#include "ps2_scancodes.h"

namespace mister::proto {

void Ps2Keyboard::push_frame(const std::uint8_t* bytes, std::uint8_t len) {

    Frame f;
    const std::uint8_t n = (len < kMaxFrameBytes) ? len : kMaxFrameBytes;
    for (std::uint8_t i = 0; i < n; ++i)
        f.bytes[i] = bytes[i];
    f.len = n;
    if (!frames_.push(f))
        ++drop_overflow_;
}

Ex<void> Ps2Keyboard::key_event(HidUsage hid_usage, bool pressed) {
    const std::uint8_t idx = hid_usage.v;

    const bool was_down = down_[idx];
    down_[idx] = pressed;
    const int press = !pressed ? 0 : (was_down ? 2 : 1);

    const std::array<std::uint32_t, 256>& table = (scan_set_ == 1) ? kEv2Ps2Set1 : kEv2Ps2Set2;
    const std::uint32_t code = table[idx];
    if (code == NONE)
        return {};

    const auto low = static_cast<std::uint8_t>(code & 0xFFu);

    if (low == 0xE1) {

        if (press != 1) {
            if (scan_set_ == 1) {
                static constexpr std::uint8_t kSeq[] = {0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5};
                push_frame(kSeq, 6);
            } else {
                static constexpr std::uint8_t kSeq[] =
                    {0xE1, 0x14, 0x77, 0xE1, 0xF0, 0x14, 0xF0, 0x77};
                push_frame(kSeq, 8);
            }
        }
        return {};
    }

    if (low == 0xE2) {

        if (press <= 1) {
            const bool make = (press == 1);
            if (scan_set_ == 1) {
                static constexpr std::uint8_t kMake[] = {0xE0, 0x2A, 0xE0, 0x37};
                static constexpr std::uint8_t kBreak[] = {0xE0, 0xB7, 0xE0, 0xAA};
                if (make)
                    push_frame(kMake, 4);
                else
                    push_frame(kBreak, 4);
            } else {
                static constexpr std::uint8_t kMake[] = {0xE0, 0x12, 0xE0, 0x7C};
                static constexpr std::uint8_t kBreak[] = {0xE0, 0xF0, 0x7C, 0xE0, 0xF0, 0x12};
                if (make)
                    push_frame(kMake, 4);
                else
                    push_frame(kBreak, 6);
            }
        }
        return {};
    }

    if (press > 1 && !ps2_control_)
        return {};

    std::array<std::uint8_t, 3> buf{};
    std::uint8_t n = 0;
    if ((code & EXT) != 0u)
        buf[n++] = 0xE0;
    std::uint8_t final_low = low;
    if (!pressed) {
        if (scan_set_ == 1)
            final_low = static_cast<std::uint8_t>(low | 0x80u);
        else
            buf[n++] = 0xF0;
    }
    buf[n++] = final_low;
    push_frame(buf.data(), n);
    return {};
}

unsigned Ps2Keyboard::release_all() {
    unsigned released = 0;
    for (std::size_t i = 0; i < down_.size(); ++i) {
        if (!down_[i])
            continue;
        (void)key_event(HidUsage{static_cast<std::uint8_t>(i)}, false);
        ++released;
    }
    return released;
}

bool Ps2Keyboard::next_frame(Ps2Frame& out) noexcept {
    if (const auto f = frames_.pop()) {
        out.stream = Ps2Stream::KeyboardFrame;
        out.len = f->len;
        out.bytes = f->bytes;
        return true;
    }
    if (const auto code = port_.next_reply()) {
        out.stream = Ps2Stream::KeyboardReply;
        out.len = 1;
        out.bytes = {};
        out.bytes[0] = *code;
        return true;
    }
    return false;
}

Ex<void> Ps2Keyboard::accept_control(std::uint16_t word0) {
    ps2_control_ = true;
    port_.accept(word0, [this](std::uint8_t byte) { decode_control(byte); });
    return {};
}

void Ps2Keyboard::end_control() noexcept {
    ps2_control_ = false;
    scan_set_ = 2;
    port_.end();
}

void Ps2Keyboard::decode_control(std::uint8_t kbd_ctl) {
    if (!port_.awaiting_param()) {
        switch (port_.command()) {
            case 0xFF:
                scan_set_ = 2;
                port_.reply(0xFA);
                port_.reply(0xAA);
                break;
            case 0xF2:
                port_.reply(0xFA);
                port_.reply(0xAB);
                port_.reply(0x83);
                break;
            case 0xF0:
                port_.reply(0xFA);
                port_.expect_param();
                break;
            case 0xF6:
                port_.reply(0xFA);
                scan_set_ = 2;
                break;
            case 0xF3:
                port_.reply(0xFA);
                port_.expect_param();
                break;
            case 0xF4:
            case 0xF5:
            case 0xFA:
                port_.reply(0xFA);
                break;
            case 0xED:
                port_.reply(0xFA);
                port_.expect_param();
                break;
            case 0xEE:
                port_.reply(0xEE);
                break;
            default:
                port_.reply(0xFE);
                break;
        }
    } else {
        switch (port_.command()) {
            case 0xED:

                port_.reply(0xFA);
                port_.param_done();
                break;
            case 0xF0:
                port_.param_done();
                if (kbd_ctl <= 3) {
                    port_.reply(0xFA);
                    if (kbd_ctl == 0)
                        port_.reply(scan_set_);
                    else
                        scan_set_ = kbd_ctl;
                } else {
                    port_.reply(0xFE);
                }
                break;
            case 0xF3:
                port_.reply(0xFA);
                port_.param_done();
                break;
            default:
                port_.param_done();
                break;
        }
    }
}
}
