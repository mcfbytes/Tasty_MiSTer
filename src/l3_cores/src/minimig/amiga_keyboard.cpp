// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/amiga_keyboard.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace mister::cores::minimig {

namespace {

constexpr auto kRows = std::to_array<AmigaKeyboard::Mapping>({
    {.code = svc::EvdevCode{1}, .key = AmigaKeyCode{0x45}},
    {.code = svc::EvdevCode{2}, .key = AmigaKeyCode{0x01}},
    {.code = svc::EvdevCode{3}, .key = AmigaKeyCode{0x02}},
    {.code = svc::EvdevCode{4}, .key = AmigaKeyCode{0x03}},
    {.code = svc::EvdevCode{5}, .key = AmigaKeyCode{0x04}},
    {.code = svc::EvdevCode{6}, .key = AmigaKeyCode{0x05}},
    {.code = svc::EvdevCode{7}, .key = AmigaKeyCode{0x06}},
    {.code = svc::EvdevCode{8}, .key = AmigaKeyCode{0x07}},
    {.code = svc::EvdevCode{9}, .key = AmigaKeyCode{0x08}},
    {.code = svc::EvdevCode{10}, .key = AmigaKeyCode{0x09}},
    {.code = svc::EvdevCode{11}, .key = AmigaKeyCode{0x0A}},
    {.code = svc::EvdevCode{12}, .key = AmigaKeyCode{0x0B}},
    {.code = svc::EvdevCode{13}, .key = AmigaKeyCode{0x0C}},
    {.code = svc::EvdevCode{14}, .key = AmigaKeyCode{0x41}},
    {.code = svc::EvdevCode{15}, .key = AmigaKeyCode{0x42}},
    {.code = svc::EvdevCode{16}, .key = AmigaKeyCode{0x10}},
    {.code = svc::EvdevCode{17}, .key = AmigaKeyCode{0x11}},
    {.code = svc::EvdevCode{18}, .key = AmigaKeyCode{0x12}},
    {.code = svc::EvdevCode{19}, .key = AmigaKeyCode{0x13}},
    {.code = svc::EvdevCode{20}, .key = AmigaKeyCode{0x14}},
    {.code = svc::EvdevCode{21}, .key = AmigaKeyCode{0x15}},
    {.code = svc::EvdevCode{22}, .key = AmigaKeyCode{0x16}},
    {.code = svc::EvdevCode{23}, .key = AmigaKeyCode{0x17}},
    {.code = svc::EvdevCode{24}, .key = AmigaKeyCode{0x18}},
    {.code = svc::EvdevCode{25}, .key = AmigaKeyCode{0x19}},
    {.code = svc::EvdevCode{26}, .key = AmigaKeyCode{0x1A}},
    {.code = svc::EvdevCode{27}, .key = AmigaKeyCode{0x1B}},
    {.code = svc::EvdevCode{28}, .key = AmigaKeyCode{0x44}},
    {.code = svc::EvdevCode{29}, .key = AmigaKeyCode{0x63}},
    {.code = svc::EvdevCode{30}, .key = AmigaKeyCode{0x20}},
    {.code = svc::EvdevCode{31}, .key = AmigaKeyCode{0x21}},
    {.code = svc::EvdevCode{32}, .key = AmigaKeyCode{0x22}},
    {.code = svc::EvdevCode{33}, .key = AmigaKeyCode{0x23}},
    {.code = svc::EvdevCode{34}, .key = AmigaKeyCode{0x24}},
    {.code = svc::EvdevCode{35}, .key = AmigaKeyCode{0x25}},
    {.code = svc::EvdevCode{36}, .key = AmigaKeyCode{0x26}},
    {.code = svc::EvdevCode{37}, .key = AmigaKeyCode{0x27}},
    {.code = svc::EvdevCode{38}, .key = AmigaKeyCode{0x28}},
    {.code = svc::EvdevCode{39}, .key = AmigaKeyCode{0x29}},
    {.code = svc::EvdevCode{40}, .key = AmigaKeyCode{0x2A}},
    {.code = svc::EvdevCode{41}, .key = AmigaKeyCode{0x00}},
    {.code = svc::EvdevCode{42}, .key = AmigaKeyCode{0x60}},
    {.code = svc::EvdevCode{43}, .key = AmigaKeyCode{0x0D}},
    {.code = svc::EvdevCode{44}, .key = AmigaKeyCode{0x31}},
    {.code = svc::EvdevCode{45}, .key = AmigaKeyCode{0x32}},
    {.code = svc::EvdevCode{46}, .key = AmigaKeyCode{0x33}},
    {.code = svc::EvdevCode{47}, .key = AmigaKeyCode{0x34}},
    {.code = svc::EvdevCode{48}, .key = AmigaKeyCode{0x35}},
    {.code = svc::EvdevCode{49}, .key = AmigaKeyCode{0x36}},
    {.code = svc::EvdevCode{50}, .key = AmigaKeyCode{0x37}},
    {.code = svc::EvdevCode{51}, .key = AmigaKeyCode{0x38}},
    {.code = svc::EvdevCode{52}, .key = AmigaKeyCode{0x39}},
    {.code = svc::EvdevCode{53}, .key = AmigaKeyCode{0x3A}},
    {.code = svc::EvdevCode{54}, .key = AmigaKeyCode{0x61}},
    {.code = svc::EvdevCode{55}, .key = AmigaKeyCode{0x5D}},
    {.code = svc::EvdevCode{56}, .key = AmigaKeyCode{0x64}},
    {.code = svc::EvdevCode{57}, .key = AmigaKeyCode{0x40}},
    {.code = svc::EvdevCode{58}, .key = AmigaKeyCode{0x62}, .caps_toggle = true},
    {.code = svc::EvdevCode{59}, .key = AmigaKeyCode{0x50}},
    {.code = svc::EvdevCode{60}, .key = AmigaKeyCode{0x51}},
    {.code = svc::EvdevCode{61}, .key = AmigaKeyCode{0x52}},
    {.code = svc::EvdevCode{62}, .key = AmigaKeyCode{0x53}},
    {.code = svc::EvdevCode{63}, .key = AmigaKeyCode{0x54}},
    {.code = svc::EvdevCode{64}, .key = AmigaKeyCode{0x55}},
    {.code = svc::EvdevCode{65}, .key = AmigaKeyCode{0x56}},
    {.code = svc::EvdevCode{66}, .key = AmigaKeyCode{0x57}},
    {.code = svc::EvdevCode{67}, .key = AmigaKeyCode{0x58}},
    {.code = svc::EvdevCode{68}, .key = AmigaKeyCode{0x59}},
    {.code = svc::EvdevCode{71}, .key = AmigaKeyCode{0x3D}},
    {.code = svc::EvdevCode{72}, .key = AmigaKeyCode{0x3E}},
    {.code = svc::EvdevCode{73}, .key = AmigaKeyCode{0x3F}},
    {.code = svc::EvdevCode{74}, .key = AmigaKeyCode{0x4A}},
    {.code = svc::EvdevCode{75}, .key = AmigaKeyCode{0x2D}},
    {.code = svc::EvdevCode{76}, .key = AmigaKeyCode{0x2E}},
    {.code = svc::EvdevCode{77}, .key = AmigaKeyCode{0x2F}},
    {.code = svc::EvdevCode{78}, .key = AmigaKeyCode{0x5E}},
    {.code = svc::EvdevCode{79}, .key = AmigaKeyCode{0x1D}},
    {.code = svc::EvdevCode{80}, .key = AmigaKeyCode{0x1E}},
    {.code = svc::EvdevCode{81}, .key = AmigaKeyCode{0x1F}},
    {.code = svc::EvdevCode{82}, .key = AmigaKeyCode{0x0F}},
    {.code = svc::EvdevCode{83}, .key = AmigaKeyCode{0x3C}},
    {.code = svc::EvdevCode{86}, .key = AmigaKeyCode{0x30}},
    {.code = svc::EvdevCode{87}, .key = AmigaKeyCode{0x5F}},
    {.code = svc::EvdevCode{96}, .key = AmigaKeyCode{0x43}},
    {.code = svc::EvdevCode{97}, .key = AmigaKeyCode{0x63}},
    {.code = svc::EvdevCode{98}, .key = AmigaKeyCode{0x5C}},
    {.code = svc::EvdevCode{100}, .key = AmigaKeyCode{0x65}},
    {.code = svc::EvdevCode{102}, .key = AmigaKeyCode{0x6A}},
    {.code = svc::EvdevCode{103}, .key = AmigaKeyCode{0x4C}},
    {.code = svc::EvdevCode{104}, .key = AmigaKeyCode{0x5B}},
    {.code = svc::EvdevCode{105}, .key = AmigaKeyCode{0x4F}},
    {.code = svc::EvdevCode{106}, .key = AmigaKeyCode{0x4E}},
    {.code = svc::EvdevCode{108}, .key = AmigaKeyCode{0x4D}},
    {.code = svc::EvdevCode{109}, .key = AmigaKeyCode{0x5A}},
    {.code = svc::EvdevCode{110}, .key = AmigaKeyCode{0x0D}},
    {.code = svc::EvdevCode{111}, .key = AmigaKeyCode{0x46}},
    {.code = svc::EvdevCode{125}, .key = AmigaKeyCode{0x66}},
    {.code = svc::EvdevCode{126}, .key = AmigaKeyCode{0x67}},
    {.code = svc::EvdevCode{183}, .key = AmigaKeyCode{0x5A}},
    {.code = svc::EvdevCode{184}, .key = AmigaKeyCode{0x5B}},
    {.code = svc::EvdevCode{186}, .key = AmigaKeyCode{0x5F}},
    {.code = svc::EvdevCode{194}, .key = AmigaKeyCode{0x2B}},
});

static_assert(
    std::ranges::is_sorted(kRows, {}, &AmigaKeyboard::Mapping::code),
    "the table is in the kernel's code order"
);

}

std::optional<AmigaKeyboard::Mapping> AmigaKeyboard::mapping_of(svc::EvdevCode code) noexcept {
    const auto* row = std::ranges::lower_bound(kRows, code, {}, &Mapping::code);
    if (row == kRows.end() || row->code != code)
        return std::nullopt;
    return *row;
}

void AmigaKeyboard::bind(Instant) noexcept {
    TASTY_SEAT_BODY(AmigaKeyboard);
    caps_down_ = false;
    gap_ends_.reset();
    queue_.clear();
    outbox_.clear();
}

void AmigaKeyboard::post_(const proto::Ps2Frame& frame) noexcept {
    if (!outbox_.push(frame))
        outbox_drops_.add();
}

void AmigaKeyboard::send_(AmigaKeyCode code, Instant now) noexcept {
    post_(code.frame());
    gap_ends_ = now + kKeyGap;
}

void AmigaKeyboard::key(svc::EvdevCode code, bool pressed, Instant now) noexcept {
    TASTY_SEAT_BODY(AmigaKeyboard);
    const std::optional<Mapping> row = mapping_of(code);
    if (!row)
        return;
    AmigaKeyCode sent = row->key;
    if (row->caps_toggle) {
        if (!pressed)
            return;
        if (caps_down_)
            sent = sent.released();
        caps_down_ = !caps_down_;
    } else if (!pressed) {
        sent = sent.released();
    }
    const bool gap_over = !gap_ends_ || now >= *gap_ends_;
    if (gap_over && queue_.empty()) {
        send_(sent, now);
        return;
    }
    if (!queue_.push(sent))
        queue_drops_.add();
}

void AmigaKeyboard::mouse(proto::MouseDelta delta, proto::MouseButtons buttons, Instant) noexcept {
    TASTY_SEAT_BODY(AmigaKeyboard);
    post_(AmigaMouseReport::of(delta, buttons).frame());
}

void AmigaKeyboard::controller_word(hal::SpiWord, Instant) noexcept {
    TASTY_SEAT_BODY(AmigaKeyboard);
}

void AmigaKeyboard::tick(Instant now) noexcept {
    TASTY_SEAT_BODY(AmigaKeyboard);
    if (gap_ends_ && now < *gap_ends_)
        return;
    gap_ends_.reset();
    if (const auto next = queue_.pop())
        send_(*next, now);
}

std::optional<IInputSink::Instant> AmigaKeyboard::deadline() const noexcept {
    TASTY_SEAT_BODY(AmigaKeyboard);
    if (queue_.empty())
        return std::nullopt;
    return gap_ends_;
}

std::optional<proto::Ps2Frame> AmigaKeyboard::next_frame() noexcept {
    TASTY_SEAT_BODY(AmigaKeyboard);
    return outbox_.pop();
}

}
