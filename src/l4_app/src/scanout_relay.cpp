// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/scanout_relay.h"

#include "hal/selected.h"

namespace mister::app {

bool ScanoutRelay::collect() noexcept {
    TASTY_SEAT_BODY(ScanoutRelay);
    if (!ask_) ask_ = channel_.take_ask();
    return ask_.has_value();
}

void ScanoutRelay::step(Wire wire) noexcept {
    TASTY_SEAT_BODY(ScanoutRelay);
    if (unsent_) {
        if (!channel_.answer(*unsent_)) return;
        unsent_.reset();
    }
    if (wire == Wire::Held || !collect()) return;
    const UioBurst ask = *ask_;
    ask_.reset();
    if (wire == Wire::CoreStarting || ask.count > UioBurst::kMaxWords) {
        refuse_(ask);
        return;
    }
    const UioBurst done = emit_(ask);
    if (!channel_.answer(done)) unsent_ = done;
}

void ScanoutRelay::refuse_(const UioBurst& ask) noexcept {
    ++refused_;
    UioBurst no = ask;
    no.ok = false;
    no.count = 0;
    if (!channel_.answer(no)) unsent_ = no;
}

UioBurst ScanoutRelay::emit_(const UioBurst& ask) noexcept {
    UioBurst out = ask;
    out.ok = true;
    ++bursts_;

    hal::Selected cs(link_, hal::ChipSelect::Io);
    ++round_words_;
    if (auto r = link_.transfer(hal::SpiWord{ask.opcode}); !r) {
        ++failures_;
        out.ok = false;
        return out;
    }
    for (std::uint8_t i = 0; i < ask.count; ++i) {
        ++round_words_;
        auto r = link_.transfer(hal::SpiWord{ask.words[i]});
        if (!r) {
            ++failures_;
            out.ok = false;
            return out;
        }
        out.words[i] = r->v;
    }
    return out;
}

}  // namespace mister::app
