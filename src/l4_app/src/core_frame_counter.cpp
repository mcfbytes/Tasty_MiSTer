// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/core_frame_counter.h"

namespace mister::app {

void CoreFrameCounter::sample_demand_() noexcept {
    if (w_.demand == nullptr) return;
    FrameDemand d{};

    if (!demand_seen_.take_if_changed(*w_.demand, d)) return;
    if (d.on == 0) based_ = false;
    demand_ = d;
}

Ex<proto::FrameCount> CoreFrameCounter::read() noexcept {
    TASTY_SEAT_BODY(CoreFrameCounter);
    if (w_.link == nullptr || w_.clock == nullptr)
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    const auto r = counter_.read(*w_.link);
    if (!r) return r;
    read_this_round_ = true;
    const std::int64_t now = w_.clock->now().count();
    if (!r->supported) {
        if (rec_.supported != 0) dirty_ = true;
        rec_.supported = 0;
        based_ = false;
        return r;
    }
    if (!based_ || rec_.supported == 0 || now - last_read_ns_ > kRebaseNs) {
        rec_.core_frame = r->count;
        rec_.edge_ns = now;
        ++rec_.epoch;
        rec_.supported = 1;
        based_ = true;
        dirty_ = true;
    } else if (const auto d = static_cast<std::uint8_t>(r->count - last_raw_); d != 0) {
        rec_.core_frame += d;
        rec_.edge_ns = now;
        dirty_ = true;
    }
    last_raw_ = r->count;
    last_read_ns_ = now;
    return r;
}

void CoreFrameCounter::settle(bool ready, std::uint32_t core_seq,
                              std::int32_t movie_frame) noexcept {
    TASTY_SEAT_BODY(CoreFrameCounter);
    sample_demand_();

    if (core_seq != rec_.core_seq) {
        rec_.core_seq = core_seq;
        based_ = false;
        dirty_ = true;
    }
    if (demanded() && ready && !read_this_round_) {
        (void)read();
        round_words_ += proto::FrameCounterRead::kReadWords;
    }
    read_this_round_ = false;
    if (!demanded()) return;
    if (movie_frame != rec_.movie_frame) {
        rec_.movie_frame = movie_frame;
        dirty_ = true;
    }
    if (dirty_ && w_.out != nullptr) w_.out->publish(rec_);
    dirty_ = false;
}

}  // namespace mister::app
