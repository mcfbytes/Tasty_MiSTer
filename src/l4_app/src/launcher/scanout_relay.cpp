// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/launcher/scanout_relay.h"

#include "hal/selected.h"

namespace mister::app::launcher {

namespace {
constexpr proto::FioWordCount kBeat{1};
}

bool ScanoutRelay::collect() noexcept {
    TASTY_SEAT_BODY(ScanoutRelay);
    if (!ask_)
        ask_ = channel_.take();
    return static_cast<bool>(ask_);
}

void ScanoutRelay::step(Wire wire) noexcept {
    TASTY_SEAT_BODY(ScanoutRelay);
    if (wire == Wire::Held || !collect())
        return;

    ScanoutChannel::Job job = std::move(ask_);
    UioBurst& burst = *job;
    if (wire == Wire::CoreStarting || burst.count > UioBurst::kMaxWords)
        refuse_(burst);
    else
        emit_(burst);
    job.complete();
}

void ScanoutRelay::refuse_(UioBurst& burst) noexcept {
    ++refused_;
    burst.ok = false;
    burst.count = 0;
}

void ScanoutRelay::emit_(UioBurst& burst) noexcept {
    burst.ok = true;
    ++bursts_;

    hal::Selected cs(link_, hal::ChipSelect::Io);
    round_words_.charge(kBeat);
    if (auto r = link_.transfer(burst.opcode); !r) {
        ++failures_;
        burst.ok = false;
        return;
    }
    for (std::uint8_t i = 0; i < burst.count; ++i) {
        round_words_.charge(kBeat);
        auto r = link_.transfer(burst.words[i]);
        if (!r) {
            ++failures_;
            burst.ok = false;
            return;
        }
        burst.words[i] = *r;
    }
}

}
