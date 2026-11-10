// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/ps2_keyboard.h"

#include <array>
#include <optional>
#include <span>

#include "ps2_scancodes.h"

namespace mister::proto {

void Ps2Keyboard::push_frame(std::span<const std::uint8_t> bytes) {

    Ps2Frame frame;
    frame.stream = Ps2Stream::KeyboardFrame;
    for (const std::uint8_t byte : bytes) {
        if (frame.len == kMaxFrameBytes)
            break;
        frame.bytes[frame.len] = byte;
        ++frame.len;
    }
    if (!frames_.push(frame))
        drops_.add();
}

Ex<void> Ps2Keyboard::key_event(HidUsage hid_usage, bool pressed) {
    const std::uint8_t idx = hid_usage.v;

    const bool was_down = down_[idx];
    down_[idx] = pressed;
    const Press press = !pressed ? Press::kRelease : (was_down ? Press::kRepeat : Press::kMake);
    const bool set1 = scan_set_ == ScanSet::kSet1;

    const std::array<std::uint32_t, 256>& table = set1 ? kEv2Ps2Set1 : kEv2Ps2Set2;
    const std::uint32_t code = table[idx];
    if (code == NONE)
        return {};

    const auto low = static_cast<std::uint8_t>(code);

    if (low == 0xE1) {

        if (press != Press::kMake) {
            if (set1) {
                static constexpr auto kSeq = std::to_array<std::uint8_t>(
                    {0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5}
                );
                push_frame(kSeq);
            } else {
                static constexpr auto kSeq = std::to_array<std::uint8_t>(
                    {0xE1, 0x14, 0x77, 0xE1, 0xF0, 0x14, 0xF0, 0x77}
                );
                push_frame(kSeq);
            }
        }
        return {};
    }

    if (low == 0xE2) {

        if (press != Press::kRepeat) {
            const bool make = press == Press::kMake;
            if (set1) {
                static constexpr auto kMake = std::to_array<std::uint8_t>({0xE0, 0x2A, 0xE0, 0x37});
                static constexpr auto kBreak = std::to_array<std::uint8_t>(
                    {0xE0, 0xB7, 0xE0, 0xAA}
                );
                if (make)
                    push_frame(kMake);
                else
                    push_frame(kBreak);
            } else {
                static constexpr auto kMake = std::to_array<std::uint8_t>({0xE0, 0x12, 0xE0, 0x7C});
                static constexpr auto kBreak = std::to_array<std::uint8_t>(
                    {0xE0, 0xF0, 0x7C, 0xE0, 0xF0, 0x12}
                );
                if (make)
                    push_frame(kMake);
                else
                    push_frame(kBreak);
            }
        }
        return {};
    }

    if (press == Press::kRepeat && !ps2_control_)
        return {};

    std::array<std::uint8_t, 3> buf{};
    std::uint8_t n = 0;
    if ((code & EXT) != 0u)
        buf[n++] = 0xE0;
    std::uint8_t final_low = low;
    if (!pressed) {
        if (set1)
            final_low = static_cast<std::uint8_t>(low | 0x80u);
        else
            buf[n++] = 0xF0;
    }
    buf[n++] = final_low;
    push_frame(std::span<const std::uint8_t>{buf}.first(n));
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

std::optional<Ps2Frame> Ps2Keyboard::next_frame() noexcept {
    if (auto frame = frames_.pop())
        return frame;
    if (const auto code = port_.next_reply()) {
        Ps2Frame frame;
        frame.stream = Ps2Stream::KeyboardReply;
        frame.len = 1;
        frame.bytes[0] = byte_of(*code);
        return frame;
    }
    return std::nullopt;
}

void Ps2Keyboard::accept_control(std::uint16_t word0) {
    ps2_control_ = true;
    port_.accept(word0, [this](std::uint8_t byte) { decode_control(byte); });
}

void Ps2Keyboard::end_control() noexcept {
    ps2_control_ = false;
    scan_set_ = ScanSet::kSet2;
    port_.end();
}

void Ps2Keyboard::decode_control(std::uint8_t kbd_ctl) {

    constexpr std::uint8_t kScanCodeGet = 0;
    constexpr auto kMaxScanSetByte = static_cast<std::uint8_t>(ScanSet::kSet3);
    if (!port_.awaiting_param()) {
        switch (port_.command()) {
            case Ps2Command::kReset:
                scan_set_ = ScanSet::kSet2;
                port_.reply(Ps2Reply::kAck);
                port_.reply(Ps2Reply::kBatOk);
                return;
            case Ps2Command::kReadId:
                port_.reply(Ps2Reply::kAck);
                port_.reply(Ps2Reply::kIdFirst);
                port_.reply(Ps2Reply::kIdSecond);
                return;
            case Ps2Command::kScanCodeOrRemote:
                port_.reply(Ps2Reply::kAck);
                port_.expect_param();
                return;
            case Ps2Command::kSetDefaults:
                port_.reply(Ps2Reply::kAck);
                scan_set_ = ScanSet::kSet2;
                return;
            case Ps2Command::kTypematicOrSampleRate:
                port_.reply(Ps2Reply::kAck);
                port_.expect_param();
                return;
            case Ps2Command::kEnable:
            case Ps2Command::kDisable:
            case Ps2Command::kEnableTypematic:
                port_.reply(Ps2Reply::kAck);
                return;
            case Ps2Command::kSetLeds:
                port_.reply(Ps2Reply::kAck);
                port_.expect_param();
                return;
            case Ps2Command::kEcho:
                port_.reply(Ps2Reply::kEcho);
                return;
            case Ps2Command::kNone:
            case Ps2Command::kSetResolution:
            case Ps2Command::kSetScaling:
            case Ps2Command::kStreamMode:
            case Ps2Command::kStatusRequest:
                port_.reply(Ps2Reply::kResend);
                return;
        }
        port_.reply(Ps2Reply::kResend);
    } else {
        switch (port_.command()) {
            case Ps2Command::kSetLeds:

                port_.reply(Ps2Reply::kAck);
                port_.param_done();
                return;
            case Ps2Command::kScanCodeOrRemote:
                port_.param_done();
                if (kbd_ctl <= kMaxScanSetByte) {
                    port_.reply(Ps2Reply::kAck);
                    if (kbd_ctl == kScanCodeGet)
                        port_.reply(scan_set_reply(scan_set_));
                    else
                        scan_set_ = scan_set_of(kbd_ctl);
                } else {
                    port_.reply(Ps2Reply::kResend);
                }
                return;
            case Ps2Command::kTypematicOrSampleRate:
                port_.reply(Ps2Reply::kAck);
                port_.param_done();
                return;
            case Ps2Command::kNone:
            case Ps2Command::kReset:
            case Ps2Command::kReadId:
            case Ps2Command::kSetDefaults:
            case Ps2Command::kEnable:
            case Ps2Command::kDisable:
            case Ps2Command::kEnableTypematic:
            case Ps2Command::kEcho:
            case Ps2Command::kSetResolution:
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
