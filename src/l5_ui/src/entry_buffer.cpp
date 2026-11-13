// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/entry_buffer.h"

#include <cstring>

namespace mister::ui {

bool EntryBuffer::push(char c) noexcept {
    TASTY_SEAT_BODY(EntryBuffer);
    if (full() || c < ' ' || c > '~')
        return false;
    text_[length_++] = c;
    return true;
}

void EntryBuffer::pop() noexcept {
    TASTY_SEAT_BODY(EntryBuffer);
    if (length_ == 0)
        return;
    --length_;
    text_[length_] = '\0';
}

void EntryBuffer::scrub() noexcept {
    TASTY_SEAT_BODY(EntryBuffer);
    explicit_bzero(text_.data(), text_.size());
    length_ = 0;
}

}
