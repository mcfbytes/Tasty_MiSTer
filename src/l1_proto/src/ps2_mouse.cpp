// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/ps2_mouse.h"

namespace mister::proto {

Ex<void> Ps2Mouse::motion(int dx, int dy, int dz, std::uint8_t buttons) {
    auto b0 = static_cast<std::uint8_t>((static_cast<std::uint8_t>(buttons) & 0x07u) | 0x08u);

    std::uint8_t bx;
    if (dx < 0)
        b0 = static_cast<std::uint8_t>(b0 | 0x10u);
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
    if (y < 0)
        b0 = static_cast<std::uint8_t>(b0 | 0x20u);
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
    p.word0 = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(b0) | ((static_cast<std::uint16_t>(w) & 0x7Fu) << 8)
    );
    p.word1 = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bx) | ((static_cast<unsigned>(buttons) << 5u) & 0x0F00u)
    );
    p.word2 = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(by) | ((static_cast<unsigned>(buttons) << 1u) & 0x0100u)
    );
    (void)packets_.push(p);
    return {};
}

bool Ps2Mouse::next_frame(Ps2Frame& out) noexcept {
    if (const auto p = packets_.pop()) {
        out.stream = Ps2Stream::MousePacket;
        out.len = 6;
        out.bytes = {};
        out.bytes[0] = static_cast<std::uint8_t>(p->word0 & 0xFFu);
        out.bytes[1] = static_cast<std::uint8_t>(p->word0 >> 8);
        out.bytes[2] = static_cast<std::uint8_t>(p->word1 & 0xFFu);
        out.bytes[3] = static_cast<std::uint8_t>(p->word1 >> 8);
        out.bytes[4] = static_cast<std::uint8_t>(p->word2 & 0xFFu);
        out.bytes[5] = static_cast<std::uint8_t>(p->word2 >> 8);
        return true;
    }
    if (const auto code = port_.next_reply()) {
        out.stream = Ps2Stream::MouseReply;
        out.len = 1;
        out.bytes = {};
        out.bytes[0] = *code;
        return true;
    }
    return false;
}

Ex<void> Ps2Mouse::accept_control(std::uint16_t word1) {
    port_.accept(word1, [this](std::uint8_t byte) { decode_control(byte); });
    return {};
}

void Ps2Mouse::end_control() noexcept {
    port_.end();
}

void Ps2Mouse::decode_control(std::uint8_t) {
    if (!port_.awaiting_param()) {
        switch (port_.command()) {
            case 0xE8:
            case 0xF3:
                port_.reply(0xFA);
                port_.expect_param();
                break;
            case 0xF2:
                port_.reply(0xFA);
                port_.reply(0x00);
                break;
            case 0xE6:
            case 0xEA:
            case 0xF0:
            case 0xF4:
            case 0xF5:
            case 0xF6:
                port_.reply(0xFA);
                break;
            case 0xE9:
                port_.reply(0xFA);
                port_.reply(0x00);
                port_.reply(0x00);
                port_.reply(0x00);
                break;
            case 0xFF:
                port_.reply(0xFA);
                port_.reply(0xAA);
                port_.reply(0x00);
                break;
            default:
                port_.reply(0xFE);
                break;
        }
    } else {
        switch (port_.command()) {
            case 0xF3:
            case 0xE8:
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
