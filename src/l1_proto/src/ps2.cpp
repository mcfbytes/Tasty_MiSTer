// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/ps2_keyboard.h"
#include "proto/ps2_frame.h"
#include "proto/ps2_wire.h"
#include "proto/ps2_mouse.h"
#include "proto/spi_ps2_decoder.h"
#include "hal/selected.h"

#include <array>
#include <cstdint>

namespace mister::proto {

namespace {

constexpr hal::SpiWord kKeyboard{0x05};
constexpr hal::SpiWord kMouse{0x04};
constexpr hal::SpiWord kGetKbdLed{0x1F};
constexpr hal::SpiWord kPs2Ctl{0x21};

constexpr std::uint32_t NONE = 0xFFu;
constexpr std::uint32_t LCTRL = 0x000100u;
constexpr std::uint32_t LSHIFT = 0x000200u;
constexpr std::uint32_t LALT = 0x000400u;
constexpr std::uint32_t LGUI = 0x000800u;
constexpr std::uint32_t RCTRL = 0x001000u;
constexpr std::uint32_t RSHIFT = 0x002000u;
constexpr std::uint32_t RALT = 0x004000u;
constexpr std::uint32_t RGUI = 0x008000u;
constexpr std::uint32_t EXT = 0x080000u;
constexpr std::uint32_t EMU_SWITCH_1 = 0x100000u;
constexpr std::uint32_t EMU_SWITCH_2 = 0x200000u;

constexpr std::array<std::uint32_t, 256> kEv2Ps2Set2 = {NONE,
                                                        0x76,
                                                        0x16,
                                                        0x1e,
                                                        0x26,
                                                        0x25,
                                                        0x2e,
                                                        0x36,
                                                        0x3d,
                                                        0x3e,
                                                        0x46,
                                                        0x45,
                                                        0x4e,
                                                        0x55,
                                                        0x66,
                                                        0x0d,
                                                        0x15,
                                                        0x1d,
                                                        0x24,
                                                        0x2d,
                                                        0x2c,
                                                        0x35,
                                                        0x3c,
                                                        0x43,
                                                        0x44,
                                                        0x4d,
                                                        0x54,
                                                        0x5b,
                                                        0x5a,
                                                        LCTRL | 0x14,
                                                        0x1c,
                                                        0x1b,
                                                        0x23,
                                                        0x2b,
                                                        0x34,
                                                        0x33,
                                                        0x3b,
                                                        0x42,
                                                        0x4b,
                                                        0x4c,
                                                        0x52,
                                                        0x0e,
                                                        LSHIFT | 0x12,
                                                        0x5d,
                                                        0x1a,
                                                        0x22,
                                                        0x21,
                                                        0x2a,
                                                        0x32,
                                                        0x31,
                                                        0x3a,
                                                        0x41,
                                                        0x49,
                                                        0x4a,
                                                        RSHIFT | 0x59,
                                                        0x7c,
                                                        LALT | 0x11,
                                                        0x29,
                                                        0x58,
                                                        0x05,
                                                        0x06,
                                                        0x04,
                                                        0x0c,
                                                        0x03,
                                                        0x0b,
                                                        0x83,
                                                        0x0a,
                                                        0x01,
                                                        0x09,
                                                        EMU_SWITCH_2 | 0x77,
                                                        EMU_SWITCH_1 | 0x7E,
                                                        0x6c,
                                                        0x75,
                                                        0x7d,
                                                        0x7b,
                                                        0x6b,
                                                        0x73,
                                                        0x74,
                                                        0x79,
                                                        0x69,
                                                        0x72,
                                                        0x7a,
                                                        0x70,
                                                        0x71,
                                                        NONE,
                                                        0x0e,
                                                        0x61,
                                                        0x78,
                                                        0x07,
                                                        0x13,
                                                        0x13,
                                                        0x13,
                                                        0x64,
                                                        0x13,
                                                        0x67,
                                                        NONE,
                                                        EXT | 0x5a,
                                                        RCTRL | EXT | 0x14,
                                                        EXT | 0x4a,
                                                        0xE2,
                                                        RALT | EXT | 0x11,
                                                        NONE,
                                                        EXT | 0x6c,
                                                        EXT | 0x75,
                                                        EXT | 0x7d,
                                                        EXT | 0x6b,
                                                        EXT | 0x74,
                                                        EXT | 0x69,
                                                        EXT | 0x72,
                                                        EXT | 0x7a,
                                                        EXT | 0x70,
                                                        EXT | 0x71,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        0xE1,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        0x6a,
                                                        LGUI | EXT | 0x1f,
                                                        RGUI | EXT | 0x27,
                                                        EXT | 0x2f,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        EMU_SWITCH_1 | 1,
                                                        EMU_SWITCH_1 | 2,
                                                        EMU_SWITCH_1 | 3,
                                                        EMU_SWITCH_1 | 4,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        0x5D,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE};

constexpr std::array<std::uint32_t, 256> kEv2Ps2Set1 = {NONE,
                                                        0x01,
                                                        0x02,
                                                        0x03,
                                                        0x04,
                                                        0x05,
                                                        0x06,
                                                        0x07,
                                                        0x08,
                                                        0x09,
                                                        0x0a,
                                                        0x0b,
                                                        0x0c,
                                                        0x0d,
                                                        0x0e,
                                                        0x0f,
                                                        0x10,
                                                        0x11,
                                                        0x12,
                                                        0x13,
                                                        0x14,
                                                        0x15,
                                                        0x16,
                                                        0x17,
                                                        0x18,
                                                        0x19,
                                                        0x1a,
                                                        0x1b,
                                                        0x1c,
                                                        LCTRL | 0x1d,
                                                        0x1e,
                                                        0x1f,
                                                        0x20,
                                                        0x21,
                                                        0x22,
                                                        0x23,
                                                        0x24,
                                                        0x25,
                                                        0x26,
                                                        0x27,
                                                        0x28,
                                                        0x29,
                                                        LSHIFT | 0x2a,
                                                        0x2b,
                                                        0x2c,
                                                        0x2d,
                                                        0x2e,
                                                        0x2f,
                                                        0x30,
                                                        0x31,
                                                        0x32,
                                                        0x33,
                                                        0x34,
                                                        0x35,
                                                        RSHIFT | 0x36,
                                                        0x37,
                                                        LALT | 0x38,
                                                        0x39,
                                                        0x3a,
                                                        0x3b,
                                                        0x3c,
                                                        0x3d,
                                                        0x3e,
                                                        0x3f,
                                                        0x40,
                                                        0x41,
                                                        0x42,
                                                        0x43,
                                                        0x44,
                                                        EMU_SWITCH_2 | 0x45,
                                                        EMU_SWITCH_1 | 0x46,
                                                        0x47,
                                                        0x48,
                                                        0x49,
                                                        0x4a,
                                                        0x4b,
                                                        0x4c,
                                                        0x4d,
                                                        0x4e,
                                                        0x4f,
                                                        0x50,
                                                        0x51,
                                                        0x52,
                                                        0x53,
                                                        NONE,
                                                        NONE,
                                                        0x56,
                                                        0x57,
                                                        0x58,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        EXT | 0x1c,
                                                        RCTRL | EXT | 0x1d,
                                                        EXT | 0x35,
                                                        0xE2,
                                                        RALT | EXT | 0x38,
                                                        NONE,
                                                        EXT | 0x47,
                                                        EXT | 0x48,
                                                        EXT | 0x49,
                                                        EXT | 0x4b,
                                                        EXT | 0x4d,
                                                        EXT | 0x4f,
                                                        EXT | 0x50,
                                                        EXT | 0x51,
                                                        EXT | 0x52,
                                                        EXT | 0x53,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        0xE1,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        LGUI | EXT | 0x5B,
                                                        RGUI | EXT | 0x5C,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        EMU_SWITCH_1 | 1,
                                                        EMU_SWITCH_1 | 2,
                                                        EMU_SWITCH_1 | 3,
                                                        EMU_SWITCH_1 | 4,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        0x2B,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE,
                                                        NONE};

}  // namespace

void Ps2Keyboard::push_frame(const std::uint8_t* bytes, std::uint8_t len) {

    if (frame_count_ >= kFrameQueueCap) {
        ++drop_overflow_;
        return;
    }
    Frame& f = frames_[frame_tail_];
    const std::uint8_t n = (len < kMaxFrameBytes) ? len : kMaxFrameBytes;
    for (std::uint8_t i = 0; i < n; ++i)
        f.bytes[i] = bytes[i];
    f.len = n;
    frame_tail_ = static_cast<std::uint8_t>((frame_tail_ + 1u) % kFrameQueueCap);
    ++frame_count_;
}

void Ps2Keyboard::push_reply(std::uint8_t code) {
    if (reply_count_ >= kReplyQueueCap) return;
    replies_[reply_tail_] = code;
    reply_tail_ = static_cast<std::uint8_t>((reply_tail_ + 1u) % kReplyQueueCap);
    ++reply_count_;
}

Ex<void> Ps2Keyboard::key_event(HidUsage hid_usage, bool pressed) {
    const std::uint8_t idx = hid_usage.v;

    const bool was_down = down_[idx];
    down_[idx] = pressed;
    const int press = !pressed ? 0 : (was_down ? 2 : 1);

    const std::array<std::uint32_t, 256>& table = (scan_set_ == 1) ? kEv2Ps2Set1 : kEv2Ps2Set2;
    const std::uint32_t code = table[idx];
    if (code == NONE) return {};

    const auto low = static_cast<std::uint8_t>(code & 0xFFu);

    if (low == 0xE1) {

        if (press != 1) {
            if (scan_set_ == 1) {
                static constexpr std::uint8_t kSeq[] = {0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5};
                push_frame(kSeq, 6);
            } else {
                static constexpr std::uint8_t kSeq[] = {0xE1, 0x14, 0x77, 0xE1,
                                                        0xF0, 0x14, 0xF0, 0x77};
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

    if (press > 1 && !ps2_control_) return {};

    std::array<std::uint8_t, 3> buf{};
    std::uint8_t n = 0;
    if ((code & EXT) != 0u) buf[n++] = 0xE0;
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
        if (!down_[i]) continue;
        (void)key_event(HidUsage{static_cast<std::uint8_t>(i)}, false);
        ++released;
    }
    return released;
}

bool Ps2Keyboard::next_frame(Ps2Frame& out) noexcept {
    if (frame_count_ > 0) {
        const Frame& f = frames_[frame_head_];
        out.stream = Ps2Stream::KeyboardFrame;
        out.len = f.len;
        out.bytes = f.bytes;
        frame_head_ = static_cast<std::uint8_t>((frame_head_ + 1u) % kFrameQueueCap);
        --frame_count_;
        return true;
    }
    if (reply_count_ > 0) {
        out.stream = Ps2Stream::KeyboardReply;
        out.len = 1;
        out.bytes = {};
        out.bytes[0] = replies_[reply_head_];
        reply_head_ = static_cast<std::uint8_t>((reply_head_ + 1u) % kReplyQueueCap);
        --reply_count_;
        return true;
    }
    return false;
}

Ex<void> Ps2Keyboard::accept_control(std::uint16_t word0) {
    ps2_control_ = true;
    if ((word0 & 0x100u) == 0u) return {};
    decode_control(static_cast<std::uint8_t>(word0 & 0xFFu));
    return {};
}

void Ps2Keyboard::end_control() noexcept {
    ps2_control_ = false;
    scan_set_ = 2;
    kbd_cmd_ = 0;
    kbd_awaiting_param_ = false;
    reply_head_ = reply_tail_ = reply_count_ = 0;
}

void Ps2Keyboard::decode_control(std::uint8_t kbd_ctl) {
    if (!kbd_awaiting_param_) {
        kbd_cmd_ = kbd_ctl;
        switch (kbd_cmd_) {
            case 0xFF:
                scan_set_ = 2;
                push_reply(0xFA);
                push_reply(0xAA);
                break;
            case 0xF2:
                push_reply(0xFA);
                push_reply(0xAB);
                push_reply(0x83);
                break;
            case 0xF0:
                push_reply(0xFA);
                kbd_awaiting_param_ = true;
                break;
            case 0xF6:
                push_reply(0xFA);
                scan_set_ = 2;
                break;
            case 0xF3:
                push_reply(0xFA);
                kbd_awaiting_param_ = true;
                break;
            case 0xF4:
            case 0xF5:
            case 0xFA:
                push_reply(0xFA);
                break;
            case 0xED:
                push_reply(0xFA);
                kbd_awaiting_param_ = true;
                break;
            case 0xEE:
                push_reply(0xEE);
                break;
            default:
                push_reply(0xFE);
                break;
        }
    } else {
        switch (kbd_cmd_) {
            case 0xED:

                push_reply(0xFA);
                kbd_awaiting_param_ = false;
                break;
            case 0xF0:
                kbd_awaiting_param_ = false;
                if (kbd_ctl <= 3) {
                    push_reply(0xFA);
                    if (kbd_ctl == 0)
                        push_reply(scan_set_);
                    else
                        scan_set_ = kbd_ctl;
                } else {
                    push_reply(0xFE);
                }
                break;
            case 0xF3:
                push_reply(0xFA);
                kbd_awaiting_param_ = false;
                break;
            default:
                kbd_awaiting_param_ = false;
                break;
        }
    }
}

void Ps2Mouse::push_packet(const Packet& p) {
    if (packet_count_ >= kPacketQueueCap) return;
    packets_[packet_tail_] = p;
    packet_tail_ = static_cast<std::uint8_t>((packet_tail_ + 1u) % kPacketQueueCap);
    ++packet_count_;
}

void Ps2Mouse::push_reply(std::uint8_t code) {
    if (reply_count_ >= kReplyQueueCap) return;
    replies_[reply_tail_] = code;
    reply_tail_ = static_cast<std::uint8_t>((reply_tail_ + 1u) % kReplyQueueCap);
    ++reply_count_;
}

Ex<void> Ps2Mouse::motion(int dx, int dy, int dz, std::uint8_t buttons) {
    auto b0 = static_cast<std::uint8_t>((static_cast<std::uint8_t>(buttons) & 0x07u) | 0x08u);

    std::uint8_t bx;
    if (dx < 0) b0 = static_cast<std::uint8_t>(b0 | 0x10u);
    if (dx < -255) {
        b0 = static_cast<std::uint8_t>(b0 | 0x40u);
        bx = 1;
    } else if (dx > 255) {
        b0 = static_cast<std::uint8_t>(b0 | 0x40u);
        bx = 255;
    } else {
        bx = static_cast<std::uint8_t>(dx);
    }

    const int y = -dy;
    std::uint8_t by;
    if (y < 0) b0 = static_cast<std::uint8_t>(b0 | 0x20u);
    if (y < -255) {
        b0 = static_cast<std::uint8_t>(b0 | 0x80u);
        by = 1;
    } else if (y > 255) {
        b0 = static_cast<std::uint8_t>(b0 | 0x80u);
        by = 255;
    } else {
        by = static_cast<std::uint8_t>(y);
    }

    int w = dz;
    if (w > 63)
        w = 63;
    else if (w < -63)
        w = -63;

    Packet p{};
    p.word0 = static_cast<std::uint16_t>(static_cast<std::uint16_t>(b0) |
                                         ((static_cast<std::uint16_t>(w) & 0x7Fu) << 8));
    p.word1 = static_cast<std::uint16_t>(static_cast<std::uint16_t>(bx) |
                                         ((static_cast<unsigned>(buttons) << 5u) & 0x0F00u));
    p.word2 = static_cast<std::uint16_t>(static_cast<std::uint16_t>(by) |
                                         ((static_cast<unsigned>(buttons) << 1u) & 0x0100u));
    push_packet(p);
    return {};
}

bool Ps2Mouse::next_frame(Ps2Frame& out) noexcept {
    if (packet_count_ > 0) {
        const Packet& p = packets_[packet_head_];
        out.stream = Ps2Stream::MousePacket;
        out.len = 6;
        out.bytes = {};
        out.bytes[0] = static_cast<std::uint8_t>(p.word0 & 0xFFu);
        out.bytes[1] = static_cast<std::uint8_t>(p.word0 >> 8);
        out.bytes[2] = static_cast<std::uint8_t>(p.word1 & 0xFFu);
        out.bytes[3] = static_cast<std::uint8_t>(p.word1 >> 8);
        out.bytes[4] = static_cast<std::uint8_t>(p.word2 & 0xFFu);
        out.bytes[5] = static_cast<std::uint8_t>(p.word2 >> 8);
        packet_head_ = static_cast<std::uint8_t>((packet_head_ + 1u) % kPacketQueueCap);
        --packet_count_;
        return true;
    }
    if (reply_count_ > 0) {
        out.stream = Ps2Stream::MouseReply;
        out.len = 1;
        out.bytes = {};
        out.bytes[0] = replies_[reply_head_];
        reply_head_ = static_cast<std::uint8_t>((reply_head_ + 1u) % kReplyQueueCap);
        --reply_count_;
        return true;
    }
    return false;
}

Ex<void> Ps2Mouse::accept_control(std::uint16_t word1) {
    ps2_control_ = true;
    if ((word1 & 0x100u) == 0u) return {};
    decode_control(static_cast<std::uint8_t>(word1 & 0xFFu));
    return {};
}

void Ps2Mouse::end_control() noexcept {
    ps2_control_ = false;
    mouse_cmd_ = 0;
    mouse_awaiting_param_ = false;
    reply_head_ = reply_tail_ = reply_count_ = 0;
}

void Ps2Mouse::decode_control(std::uint8_t mouse_ctl) {
    if (!mouse_awaiting_param_) {
        mouse_cmd_ = mouse_ctl;
        switch (mouse_cmd_) {
            case 0xE8:
            case 0xF3:
                push_reply(0xFA);
                mouse_awaiting_param_ = true;
                break;
            case 0xF2:
                push_reply(0xFA);
                push_reply(0x00);
                break;
            case 0xE6:
            case 0xEA:
            case 0xF0:
            case 0xF4:
            case 0xF5:
            case 0xF6:
                push_reply(0xFA);
                break;
            case 0xE9:
                push_reply(0xFA);
                push_reply(0x00);
                push_reply(0x00);
                push_reply(0x00);
                break;
            case 0xFF:
                push_reply(0xFA);
                push_reply(0xAA);
                push_reply(0x00);
                break;
            default:
                push_reply(0xFE);
                break;
        }
    } else {
        switch (mouse_cmd_) {
            case 0xF3:
            case 0xE8:
                push_reply(0xFA);
                mouse_awaiting_param_ = false;
                break;
            default:
                mouse_awaiting_param_ = false;
                break;
        }
    }
}

[[nodiscard]] Ex<void> write_ps2_frame(hal::ISpiTransport& link, const Ps2Frame& f) {
    if (f.len == 0 || f.len > kPs2FrameBytes) {
        return std::unexpected(Error{Errc::bad_opcode, ERR_SITE(), f.len});
    }
    const bool mouse = f.stream == Ps2Stream::MousePacket || f.stream == Ps2Stream::MouseReply;
    const bool reply = f.stream == Ps2Stream::KeyboardReply || f.stream == Ps2Stream::MouseReply;
    if (f.stream == Ps2Stream::MousePacket && (f.len % 2u) != 0u) {
        return std::unexpected(Error{Errc::bad_opcode, ERR_SITE(), f.len});
    }
    hal::Selected cs(link, hal::ChipSelect::Io);
    if (auto r = link.transfer(mouse ? kMouse : kKeyboard); !r) {
        return std::unexpected(r.error());
    }
    if (f.stream == Ps2Stream::MousePacket) {
        for (std::uint8_t i = 0; i + 1u < f.len; i = static_cast<std::uint8_t>(i + 2u)) {
            const auto word = static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(f.bytes[i]) |
                static_cast<std::uint16_t>(static_cast<std::uint16_t>(f.bytes[i + 1]) << 8));
            if (auto r = link.transfer(hal::SpiWord{word}); !r) {
                return std::unexpected(r.error());
            }
        }
        return {};
    }
    for (std::uint8_t i = 0; i < f.len; ++i) {
        const auto word = static_cast<std::uint16_t>(reply ? (0xFF00u | f.bytes[i]) : f.bytes[i]);
        if (auto r = link.transfer(hal::SpiWord{word}); !r) {
            return std::unexpected(r.error());
        }
    }
    return {};
}

void SpiPs2Decoder::probe() noexcept {
    if (ps2_control_) return;
    if (led_poll_div_ != 0) {
        --led_poll_div_;
        return;
    }
    led_poll_div_ = kLedPollTicks - 1;
    hal::Selected cs(*link_, hal::ChipSelect::Io);
    if (auto r = link_->transfer(kGetKbdLed); !r) return count_error_();
    auto w = link_->transfer(hal::SpiWord{0});
    if (!w) return count_error_();
    if ((w->v & 0x100u) != 0u) ps2_control_ = true;
}

void SpiPs2Decoder::forget() noexcept {
    ps2_control_ = false;
    led_poll_div_ = 0;
    if (!out_.push(LinkEvent::Ps2ControlEnded{})) count_error_();
}

void SpiPs2Decoder::service() noexcept {
    if (!ps2_control_) return;

    hal::Selected cs(*link_, hal::ChipSelect::Io);
    if (auto r = link_->transfer(kPs2Ctl); !r) return count_error_();
    auto w0 = link_->transfer(hal::SpiWord{0});
    if (!w0) return count_error_();
    auto w1 = link_->transfer(hal::SpiWord{0});
    if (!w1) return count_error_();

    if (!out_.push(LinkEvent::Ps2Control{.keyboard = w0->v, .mouse = w1->v})) count_error_();
}

}  // namespace mister::proto
