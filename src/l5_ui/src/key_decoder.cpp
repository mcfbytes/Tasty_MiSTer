// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/key_decoder.h"

#include <chrono>
#include <cstddef>
#include <linux/input-event-codes.h>

#include "os/clock.h"
#include "svc/evdev_range.h"
#include "ui/ui_event.h"

namespace mister::ui {

namespace {

struct DecodeRow {
    svc::EvdevCode code;
    KeyEdge edge;
    std::uint16_t button;
};

constexpr DecodeRow kFullTable[] = {
    {KEY_F12, KeyEdge::Break, UiEvent::Menu},
    {KEY_ESC, KeyEdge::Break, UiEvent::Menu},

    {KEY_BACK, KeyEdge::Break, UiEvent::Menu | UiEvent::PadBack},
    {KEY_BACKSPACE, KeyEdge::Break, UiEvent::Back},
    {KEY_ENTER, KeyEdge::Make, UiEvent::Select},
    {KEY_SPACE, KeyEdge::Make, UiEvent::Select},
    {KEY_KPENTER, KeyEdge::Make, UiEvent::Select},
    {KEY_UP, KeyEdge::Make, UiEvent::Up},
    {KEY_DOWN, KeyEdge::Make, UiEvent::Down},
    {KEY_LEFT, KeyEdge::Make, UiEvent::Left},
    {KEY_RIGHT, KeyEdge::Make, UiEvent::Right},
    {KEY_KPPLUS, KeyEdge::Make, UiEvent::Plus},
    {KEY_EQUAL, KeyEdge::Make, UiEvent::Plus},
    {KEY_KPMINUS, KeyEdge::Make, UiEvent::Minus},
    {KEY_MINUS, KeyEdge::Make, UiEvent::Minus},
    {KEY_GRAVE, KeyEdge::Make, UiEvent::Recent},
};

constexpr DecodeRow kIniSelectTable[] = {
    {KEY_ESC, KeyEdge::Break, UiEvent::Menu},
    {KEY_UP, KeyEdge::Make, UiEvent::Up},
    {KEY_DOWN, KeyEdge::Make, UiEvent::Down},
    {KEY_LEFT, KeyEdge::Make, UiEvent::Left},
    {KEY_RIGHT, KeyEdge::Make, UiEvent::Right},
};

template <std::size_t N>
consteval bool one_row_per_edge(const DecodeRow (&t)[N]) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
        for (std::size_t j = i + 1; j < N; ++j) {
            if (t[i].code == t[j].code && t[i].edge == t[j].edge)
                return false;
        }
    }
    return true;
}

static_assert(one_row_per_edge(kFullTable));
static_assert(one_row_per_edge(kIniSelectTable));

template <std::size_t N>
std::uint16_t lookup(const DecodeRow (&t)[N], proto::KeyCode code) noexcept {
    const KeyEdge edge = code.is_release() ? KeyEdge::Break : KeyEdge::Make;
    const svc::EvdevCode k = svc::EvdevCode::of(code);
    for (const DecodeRow& r : t) {
        if (r.code == k && r.edge == edge)
            return r.button;
    }
    return UiEvent::None;
}

bool ascii_printable(proto::KeyCode code) noexcept {
    if (code.is_release())
        return false;
    const svc::EvdevCode k = svc::EvdevCode::of(code);
    using E = svc::EvdevCode;
    constexpr svc::EvdevRange kAsciiRows[] = {
        {E{KEY_1}, E{KEY_0}},
        {E{KEY_Q}, E{KEY_P}},
        {E{KEY_A}, E{KEY_L}},
        {E{KEY_Z}, E{KEY_M}},
    };
    for (const svc::EvdevRange& row : kAsciiRows) {
        if (row.contains(k))
            return true;
    }
    return k == E{KEY_MINUS} || k == E{KEY_EQUAL} || k == E{KEY_SPACE};
}

bool is_cursor(proto::KeyCode code) noexcept {
    if (code.is_release())
        return false;
    const svc::EvdevCode k = svc::EvdevCode::of(code);
    using E = svc::EvdevCode;
    return k == E{KEY_UP} || k == E{KEY_DOWN} || k == E{KEY_LEFT} || k == E{KEY_RIGHT};
}

bool held_make(proto::KeyCode code) noexcept {
    return !code.is_release();
}

}

UiButtonMask KeyDecoder::decode(proto::KeyCode code, bool ini_select) noexcept {
    return UiButtonMask{ini_select ? lookup(kIniSelectTable, code) : lookup(kFullTable, code)};
}

bool KeyDecoder::repeatable(proto::KeyCode code) noexcept {
    return ascii_printable(code) || is_cursor(code);
}

void KeyDecoder::push_raw(proto::KeyCode code) noexcept {
    enqueue(code, false);
}

void KeyDecoder::push_synth(proto::KeyCode code) noexcept {
    enqueue(code, true);
}

void KeyDecoder::enqueue(proto::KeyCode code, bool synth) noexcept {

    if (code == proto::KeyCode{})
        return;
    if (ring_.push(QueuedKey{.code = code, .synth = synth}))
        ++dropped_;
}

void KeyDecoder::reset() noexcept {
    ring_ = KeyRing{};
    last_seen_ = proto::KeyCode{};
    db_pending_.reset();
    db_at_ = std::chrono::nanoseconds{};
    repeat_at_ = std::chrono::nanoseconds{};
    hold_cnt_ = 0;
}

std::optional<UiEvent> KeyDecoder::deliver_(
    proto::KeyCode code,
    std::chrono::nanoseconds now,
    bool first
) noexcept {
    std::optional<proto::KeyCode> c;
    if (first) {
        c = code;
        hold_cnt_ = 1;
        repeat_at_ = now + kRepeatDelay;
    }
    if (code.is_release()) {
        hold_cnt_ = 0;
        repeat_at_ = now + kRepeatDelay;
    } else if (now >= repeat_at_) {
        repeat_at_ = now + kRepeatRate;
        if (repeatable(code) || repeat_all_) {
            c = code;
            ++hold_cnt_;
        }
    }
    if (!c)
        return std::nullopt;

    UiEvent ev{};
    ev.raw = *c;
    ev.buttons = decode(*c, ini_select_);
    ev.repeat_depth = hold_cnt_;
    return ev;
}

std::optional<UiEvent> KeyDecoder::accept_(
    proto::KeyCode code,
    std::chrono::nanoseconds now
) noexcept {
    last_seen_ = code;
    db_at_ = now + kDebounce;
    return deliver_(code, now, true);
}

std::optional<UiEvent> KeyDecoder::pop() noexcept {
    const std::chrono::nanoseconds now = clock_->now().time_since_epoch();

    if (db_pending_.has_value() && now >= db_at_) {
        const proto::KeyCode bounced = *db_pending_;
        db_pending_.reset();
        db_at_ = std::chrono::nanoseconds{};

        if (held_make(bounced) && ring_.empty()) {
            (void)ring_.push(QueuedKey{.code = bounced, .synth = false});
        }
        if (bounced != last_seen_)
            return accept_(bounced, now);
    }
    if (ring_.empty())
        return std::nullopt;

    if (!ring_.head().synth && now < db_at_) {
        bool swallowed = false;
        while (!ring_.empty() && !ring_.head().synth) {
            const proto::KeyCode c = ring_.head().code;
            if (!swallowed && ring_.size() == 1 && held_make(c) && c == last_seen_)
                break;
            db_pending_ = c;
            (void)ring_.take();
            swallowed = true;
        }
        if (swallowed || ring_.empty())
            return std::nullopt;
    }

    std::optional<UiEvent> ev;
    for (;;) {
        const proto::KeyCode c1 = ring_.head().code;
        if (ring_.head().synth) {
            ev = deliver_(c1, now, true);
        } else if (c1 != last_seen_) {
            ev = accept_(c1, now);
        } else {
            ev = deliver_(c1, now, false);
        }
        const bool stays = held_make(c1) && ring_.size() == 1;
        if (!stays)
            (void)ring_.take();
        if (ev.has_value() || stays || ring_.empty())
            return ev;
    }
}

}
