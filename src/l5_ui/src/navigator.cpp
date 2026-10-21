// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/navigator.h"
#include "ui/key_decoder.h"
#include "ui/ui_event.h"

#include <cstddef>
#include <linux/input-event-codes.h>

#include "os/clock.h"

namespace mister::ui {
namespace {

constexpr std::uint64_t bit(std::uint32_t i) noexcept {
    return i < 64 ? (std::uint64_t{1} << i) : 0u;
}

}

bool Navigator::selectable(RowIndex i) const noexcept {
    return (mask_.v & bit(i.v)) != 0;
}

bool Navigator::move(Direction d) noexcept {
    if (mask_.v == 0)
        return false;

    const std::uint32_t before = index_.v;
    std::uint32_t idx = before;

    if (d == Direction::Down) {
        const std::uint64_t above = (idx + 1 < 64) ? bit(idx + 1) : 0u;
        if (above != 0 && mask_.v >= above) {
            do {
                ++idx;
            } while (idx < 64 && !selectable(RowIndex{idx}));
            if (idx >= 64)
                idx = before;
        } else {
            idx = 0;
            while (idx < 64 && !selectable(RowIndex{idx}))
                ++idx;
            if (idx >= 64)
                idx = before;
        }
    } else {
        const bool entered_at_zero = (idx == 0);
        if (!entered_at_zero) {
            do {
                --idx;
            } while (idx != 0 && !selectable(RowIndex{idx}));
        }
        if (entered_at_zero || (idx == 0 && !selectable(RowIndex{0}))) {
            std::uint32_t highest = idx;
            for (std::uint32_t i = 0; i < 64; ++i) {
                if (selectable(RowIndex{i}))
                    highest = i;
            }
            idx = highest;
        }
    }

    index_ = RowIndex{idx};
    return idx != before;
}

namespace {

struct DecodeRow {
    std::uint16_t code;
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
std::uint16_t lookup(const DecodeRow (&t)[N], std::uint32_t code) noexcept {
    const KeyEdge edge = (code & KeyDecoder::kUpstroke) != 0 ? KeyEdge::Break : KeyEdge::Make;
    const std::uint16_t k = static_cast<std::uint16_t>(code & 0xFFFFu);
    for (const DecodeRow& r : t) {
        if (r.code == k && r.edge == edge)
            return r.button;
    }
    return UiEvent::None;
}

bool ascii_printable(std::uint32_t code) noexcept {
    if ((code & KeyDecoder::kUpstroke) != 0)
        return false;
    const std::uint32_t k = code & 0xFFFFu;
    if (k >= KEY_1 && k <= KEY_0)
        return true;
    if (k >= KEY_Q && k <= KEY_P)
        return true;
    if (k >= KEY_A && k <= KEY_L)
        return true;
    if (k >= KEY_Z && k <= KEY_M)
        return true;
    return k == KEY_MINUS || k == KEY_EQUAL || k == KEY_SPACE;
}

bool is_cursor(std::uint32_t code) noexcept {
    if ((code & KeyDecoder::kUpstroke) != 0)
        return false;
    const std::uint32_t k = code & 0xFFFFu;
    return k == KEY_UP || k == KEY_DOWN || k == KEY_LEFT || k == KEY_RIGHT;
}

bool held_make(std::uint32_t code) noexcept {
    return (code & KeyDecoder::kUpstroke) == 0 && code != 0;
}

std::int64_t ms_ns(unsigned ms) noexcept {
    return static_cast<std::int64_t>(ms) * 1000000;
}

}

std::uint16_t KeyDecoder::decode(std::uint32_t code, bool ini_select) noexcept {
    return ini_select ? lookup(kIniSelectTable, code) : lookup(kFullTable, code);
}

bool KeyDecoder::repeatable(std::uint32_t code) noexcept {
    return ascii_printable(code) || is_cursor(code);
}

void KeyDecoder::push_raw(proto::KeyCode code) noexcept {
    enqueue(code, false);
}

void KeyDecoder::push_synth(proto::KeyCode code) noexcept {
    enqueue(code, true);
}

void KeyDecoder::enqueue(proto::KeyCode code, bool synth) noexcept {
    if (count_ == kQueue) {
        head_ = (head_ + 1) % kQueue;
        --count_;
        ++dropped_;
    }
    const unsigned tail = (head_ + count_) % kQueue;
    slots_[tail] = code.v;
    synth_[tail] = synth;
    ++count_;
}

void KeyDecoder::reset() noexcept {
    head_ = 0;
    count_ = 0;
    last_seen_ = 0;
    db_pending_ = 0;
    db_at_ns_ = 0;
    repeat_at_ns_ = 0;
    hold_cnt_ = 0;
}

std::optional<UiEvent> KeyDecoder::deliver_(
    std::uint32_t code,
    std::int64_t now,
    bool first
) noexcept {
    std::uint32_t c = 0;
    if (first) {
        c = code;
        hold_cnt_ = 1;
        repeat_at_ns_ = now + ms_ns(kRepeatDelayMs);
    }
    if ((code & kUpstroke) != 0 || code == 0) {
        hold_cnt_ = 0;
        repeat_at_ns_ = now + ms_ns(kRepeatDelayMs);
    } else if (now >= repeat_at_ns_) {
        repeat_at_ns_ = now + ms_ns(kRepeatRateMs);
        if (repeatable(code) || repeat_all_) {
            c = code;
            ++hold_cnt_;
        }
    }
    if (c == 0)
        return std::nullopt;

    UiEvent ev{};
    ev.kind = UiEvent::Kind::Key;
    ev.raw = proto::KeyCode{c};
    ev.buttons = decode(c, ini_select_);
    ev.repeat_depth = hold_cnt_;
    return ev;
}

std::optional<UiEvent> KeyDecoder::accept_(std::uint32_t code, std::int64_t now) noexcept {
    last_seen_ = code;
    db_at_ns_ = now + ms_ns(kDebounceMs);
    return deliver_(code, now, true);
}

std::optional<UiEvent> KeyDecoder::pop() noexcept {
    const std::int64_t now = clock_->now().count();

    if (db_pending_ != 0 && now >= db_at_ns_) {
        const std::uint32_t bounced = db_pending_;
        db_pending_ = 0;
        db_at_ns_ = 0;

        if (held_make(bounced) && count_ == 0) {
            slots_[head_] = bounced;
            synth_[head_] = false;
            count_ = 1;
        }
        if (bounced != last_seen_)
            return accept_(bounced, now);
    }
    if (count_ == 0)
        return std::nullopt;

    if (!synth_[head_] && now < db_at_ns_) {
        bool swallowed = false;
        while (count_ != 0 && !synth_[head_]) {
            const std::uint32_t c = slots_[head_];
            if (!swallowed && count_ == 1 && held_make(c) && c == last_seen_)
                break;
            db_pending_ = c;
            head_ = (head_ + 1) % kQueue;
            --count_;
            swallowed = true;
        }
        if (swallowed || count_ == 0)
            return std::nullopt;
    }

    std::optional<UiEvent> ev;
    for (;;) {
        const std::uint32_t c1 = slots_[head_];
        if (synth_[head_]) {
            ev = deliver_(c1, now, true);
        } else if (c1 != last_seen_) {
            ev = accept_(c1, now);
        } else {
            ev = deliver_(c1, now, false);
        }
        const bool stays = held_make(c1) && count_ == 1;
        if (!stays) {
            head_ = (head_ + 1) % kQueue;
            --count_;
        }
        if (ev.has_value() || stays || count_ == 0)
            return ev;
    }
}

}
