// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/status_register.h"
#include "proto/uio_opcodes.h"

namespace mister::proto {

namespace {
constexpr std::uint16_t kSetStatus2 = 0x1E;
}

void StatusRegister::set_bit(StatusBit bit, bool value) {
    if (bit.v >= kBits) {
        fatal(Error{Errc::slot_range, ERR_SITE(), bit.v}, "status bit");
    }
    const unsigned w = bit.v / 16u, b = bit.v % 16u;
    const auto mask = static_cast<std::uint16_t>(1u << b);
    const std::uint16_t next = value
        ? static_cast<std::uint16_t>(words_[w] | mask)
        : static_cast<std::uint16_t>(words_[w] & static_cast<std::uint16_t>(~mask));
    if (next != words_[w]) {
        words_[w] = next;
        dirty_ = true;
    }
}

void StatusRegister::adopt(std::span<const std::uint16_t, kWords> words) noexcept {
    for (unsigned i = 0; i < kWords; ++i)
        words_[i] = words[i];
    dirty_ = true;
}

void StatusRegister::write(const StatusEdit& edit) noexcept {
    const StatusWord next = edit.over(value());
    for (unsigned i = 0; i < kWords; ++i) {
        if (next.words[i] != words_[i]) {
            words_[i] = next.words[i];
            dirty_ = true;
        }
    }
}

StatusWord StatusRegister::value() const noexcept {
    StatusWord out{};
    static_assert(StatusWord::kWords == kWords);
    for (unsigned i = 0; i < kWords; ++i)
        out.words[i] = words_[i];
    return out;
}

bool StatusRegister::get_bit(StatusBit bit) const {
    if (bit.v >= kBits) {
        fatal(Error{Errc::slot_range, ERR_SITE(), bit.v}, "status bit");
    }
    return ((static_cast<unsigned>(words_[bit.v / 16u]) >> (bit.v % 16u)) & 1u) != 0u;
}

Ex<void> StatusRegister::flush(ILinkFrame& frame) {
    const StatusWord next = value();
    if (fence_)
        fence_->before_status(sent_, next);
    if (auto r = frame.begin(FrameSel::Uio, static_cast<std::uint16_t>(UioOp::SetStatus2)); !r)
        return r;
    for (unsigned i = 0; i < kWords; ++i) {
        if (auto r = frame.word(words_[i]); !r)
            return r;
    }
    if (auto r = frame.end(); !r)
        return r;
    dirty_ = false;
    sent_ = next;
    return {};
}

}
