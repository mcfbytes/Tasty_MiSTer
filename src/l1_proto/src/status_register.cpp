// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/status_register.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

void StatusRegister::set_bit(StatusBit bit, bool value) {
    if (bit.v >= kBits) {
        fatal(Error{Errc::slot_range, ERR_SITE(), bit.v}, "status bit");
    }
    const StatusWord next = shadow_.with(bit, value);
    if (next != shadow_) {
        shadow_ = next;
        dirty_ = true;
    }
}

void StatusRegister::adopt(const StatusWord& word) noexcept {
    shadow_ = word;
    dirty_ = true;
}

void StatusRegister::write(const StatusEdit& edit) noexcept {
    const StatusWord next = edit.over(value());
    for (unsigned i = 0; i < kWords; ++i) {
        if (next.words[i] != shadow_.words[i]) {
            shadow_.words[i] = next.words[i];
            dirty_ = true;
        }
    }
}

StatusWord StatusRegister::value() const noexcept {
    return shadow_;
}

bool StatusRegister::get_bit(StatusBit bit) const {
    if (bit.v >= kBits) {
        fatal(Error{Errc::slot_range, ERR_SITE(), bit.v}, "status bit");
    }
    return shadow_.has(bit);
}

Ex<void> StatusRegister::flush(ILinkFrame& frame) {
    const StatusWord next = value();
    if (fence_)
        fence_->before_status(sent_, next);
    if (auto r = frame.begin(UioOp::SetStatus2); !r)
        return r;
    for (unsigned i = 0; i < kWords; ++i) {
        if (auto r = frame.word(shadow_.words[i]); !r)
            return r;
    }
    if (auto r = frame.end(); !r)
        return r;
    dirty_ = false;
    sent_ = next;
    return {};
}

}
