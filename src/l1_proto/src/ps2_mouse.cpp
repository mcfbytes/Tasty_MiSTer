// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/ps2_mouse.h"

#include <span>

#include "infra/endian.h"

namespace mister::proto {

namespace {

constexpr std::uint8_t kButtonMask = 0x07;
constexpr std::uint8_t kAlwaysSet = 0x08;
constexpr std::uint8_t kXSign = 0x10;
constexpr std::uint8_t kYSign = 0x20;
constexpr std::uint8_t kXOverflow = 0x40;
constexpr std::uint8_t kYOverflow = 0x80;

constexpr std::uint8_t kWheelMask = 0x7F;
constexpr std::uint8_t kWheelMax = 63;
constexpr std::uint8_t kExtentMax = 255;

constexpr std::uint8_t kOverflowByte = 1;
constexpr unsigned kWheelShift = 8;

constexpr std::uint16_t kMidButtonBits = 0x0F00;
constexpr unsigned kMidButtonShift = 5;
constexpr std::uint16_t kRightButtonBits = 0x0100;
constexpr unsigned kRightButtonShift = 1;

}

Ps2Mouse::Packet Ps2Mouse::Packet::of(MouseDelta delta, MouseButtons buttons) noexcept {
    const auto [dx, dy, wheel] = delta;
    std::uint8_t flags = static_cast<std::uint8_t>((buttons.v & kButtonMask) | kAlwaysSet);

    std::uint8_t bx = 0;
    if (dx < 0)
        flags = static_cast<std::uint8_t>(flags | kXSign);
    if (dx < -kExtentMax) {
        flags = static_cast<std::uint8_t>(flags | kXOverflow);
        bx = kOverflowByte;
    } else if (dx > kExtentMax) {
        flags = static_cast<std::uint8_t>(flags | kXOverflow);
        bx = kExtentMax;
    } else {
        bx = static_cast<std::uint8_t>(dx);
    }

    const int y = -dy;
    std::uint8_t by = 0;
    if (y < 0)
        flags = static_cast<std::uint8_t>(flags | kYSign);
    if (y < -kExtentMax) {
        flags = static_cast<std::uint8_t>(flags | kYOverflow);
        by = kOverflowByte;
    } else if (y > kExtentMax) {
        flags = static_cast<std::uint8_t>(flags | kYOverflow);
        by = kExtentMax;
    } else {
        by = static_cast<std::uint8_t>(y);
    }

    const int w = wheel > kWheelMax ? kWheelMax : wheel < -kWheelMax ? -kWheelMax : wheel;

    Packet p{};
    p.word0 = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(flags)
        | static_cast<std::uint16_t>((static_cast<std::uint16_t>(w) & kWheelMask) << kWheelShift)
    );
    p.word1 = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bx)
        | static_cast<std::uint16_t>(
            (static_cast<unsigned>(buttons.v) << kMidButtonShift) & kMidButtonBits
        )
    );
    p.word2 = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(by)
        | static_cast<std::uint16_t>(
            (static_cast<unsigned>(buttons.v) << kRightButtonShift) & kRightButtonBits
        )
    );
    return p;
}

Ex<void> Ps2Mouse::motion(MouseDelta delta, MouseButtons buttons) {
    (void)packets_.push(Packet::of(delta, buttons));
    return {};
}

bool Ps2Mouse::next_frame(Ps2Frame& out) noexcept {
    if (const auto p = packets_.pop()) {
        out.stream = Ps2Stream::MousePacket;
        out.len = 6;
        out.bytes = {};
        const std::span<std::uint8_t, kPs2FrameBytes> bytes{out.bytes};
        constexpr std::size_t kWordBytes = 2;
        infra::store_le<std::uint16_t>(bytes.first<kWordBytes>(), p->word0);
        infra::store_le<std::uint16_t>(bytes.subspan(kWordBytes).first<kWordBytes>(), p->word1);
        infra::store_le<std::uint16_t>(bytes.subspan(2 * kWordBytes).first<kWordBytes>(), p->word2);
        return true;
    }
    if (const auto code = port_.next_reply()) {
        out.stream = Ps2Stream::MouseReply;
        out.len = 1;
        out.bytes = {};
        out.bytes[0] = byte_of(*code);
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
            case Ps2Command::kSetResolution:
            case Ps2Command::kTypematicOrSampleRate:
                port_.reply(Ps2Reply::kAck);
                port_.expect_param();
                return;
            case Ps2Command::kReadId:
                port_.reply(Ps2Reply::kAck);
                port_.reply(Ps2Reply::kZero);
                return;
            case Ps2Command::kSetScaling:
            case Ps2Command::kStreamMode:
            case Ps2Command::kScanCodeOrRemote:
            case Ps2Command::kEnable:
            case Ps2Command::kDisable:
            case Ps2Command::kSetDefaults:
                port_.reply(Ps2Reply::kAck);
                return;
            case Ps2Command::kStatusRequest:
                port_.reply(Ps2Reply::kAck);
                port_.reply(Ps2Reply::kZero);
                port_.reply(Ps2Reply::kZero);
                port_.reply(Ps2Reply::kZero);
                return;
            case Ps2Command::kReset:
                port_.reply(Ps2Reply::kAck);
                port_.reply(Ps2Reply::kBatOk);
                port_.reply(Ps2Reply::kZero);
                return;
            case Ps2Command::kNone:
            case Ps2Command::kSetLeds:
            case Ps2Command::kEnableTypematic:
            case Ps2Command::kEcho:
                port_.reply(Ps2Reply::kResend);
                return;
        }
        port_.reply(Ps2Reply::kResend);
    } else {
        switch (port_.command()) {
            case Ps2Command::kTypematicOrSampleRate:
            case Ps2Command::kSetResolution:
                port_.reply(Ps2Reply::kAck);
                port_.param_done();
                return;
            case Ps2Command::kNone:
            case Ps2Command::kReset:
            case Ps2Command::kReadId:
            case Ps2Command::kSetDefaults:
            case Ps2Command::kSetLeds:
            case Ps2Command::kScanCodeOrRemote:
            case Ps2Command::kEnable:
            case Ps2Command::kDisable:
            case Ps2Command::kEnableTypematic:
            case Ps2Command::kEcho:
            case Ps2Command::kSetScaling:
            case Ps2Command::kStreamMode:
            case Ps2Command::kStatusRequest:
                port_.param_done();
                return;
        }
        port_.param_done();
    }
}
}
