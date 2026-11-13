// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <chrono>
#include <optional>

#include "app/types.h"
#include "hal/types.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/clock.h"

namespace mister::app {

class FrameReference {
    TASTY_SEAT_EXEMPT(component);

public:
    using Instant = os::RawMonotonicClock::time_point;

    struct Dating {
        std::chrono::nanoseconds period{};
        std::chrono::nanoseconds gap{};
    };

    struct Step {
        CoreFrameCount edges{};

        [[nodiscard]] constexpr bool moved() const noexcept { return edges != CoreFrameCount{}; }

        [[nodiscard]] constexpr bool single() const noexcept { return edges == CoreFrameCount{1}; }

        [[nodiscard]] constexpr CoreFrameCount skipped() const noexcept {
            return moved() ? edges - CoreFrameCount{1} : CoreFrameCount{};
        }
    };

    struct VsyncSample {
        hal::FrameSeq seq{};
        Instant edge{};
    };

    FrameReference() noexcept = default;

    [[nodiscard]] static FrameReference at(Wrapping raw, Instant now) noexcept;

    [[nodiscard]] bool lost(Instant now, std::chrono::nanoseconds alias) const noexcept;

    [[nodiscard]] Step sample(
        Wrapping raw,
        Instant now,
        infra::OptRef<const Dating> dating
    ) noexcept;

    [[nodiscard]] Step sample(VsyncSample vsync, Instant now) noexcept;

    [[nodiscard]] std::chrono::nanoseconds lapse_since_look() noexcept;

    [[nodiscard]] CountParity parity() const noexcept { return last_.parity(); }

    [[nodiscard]] CoreFrameCount frames() const noexcept { return frames_; }

    [[nodiscard]] Instant edge() const noexcept { return edge_; }

    [[nodiscard]] std::chrono::nanoseconds edge_gap() const noexcept { return edge_gap_; }

private:
    Step fold_(Wrapping raw, Instant now) noexcept;
    Step dated_(Wrapping raw, Instant now, infra::OptRef<const Dating> dating) noexcept;
    void close_(Instant edge) noexcept;

    Wrapping last_{};
    CoreFrameCount frames_{};
    Instant edge_{}, last_moved_{}, prev_look_{};
    std::optional<Instant> read_{};
    std::chrono::nanoseconds edge_gap_{};
};

inline FrameReference FrameReference::at(Wrapping raw, Instant now) noexcept {
    FrameReference r;
    r.last_ = raw;
    r.edge_ = r.last_moved_ = r.prev_look_ = now;
    r.read_ = now;
    return r;
}

inline bool FrameReference::lost(Instant now, std::chrono::nanoseconds alias) const noexcept {
    return now - last_moved_ > alias;
}

inline FrameReference::Step FrameReference::fold_(Wrapping raw, Instant now) noexcept {
    read_ = now;
    if (raw == last_)
        return Step{};
    last_moved_ = now;
    const CoreFrameCount d = raw.since(last_);
    last_ = raw;
    frames_ += d;
    return Step{d};
}

inline FrameReference::Step FrameReference::dated_(
    Wrapping raw,
    Instant now,
    infra::OptRef<const Dating> dating
) noexcept {
    const std::optional<Instant> prev_read = read_;
    const Step step = fold_(raw, now);
    if (!step.moved())
        return step;
    Instant edge = now;
    if (dating && prev_read && now - *prev_read > dating->gap)
        edge = std::min(now, std::max(*prev_read, edge_ + step.edges.lasting(dating->period)));
    close_(edge);
    return step;
}

inline FrameReference::Step FrameReference::sample(
    Wrapping raw,
    Instant now,
    infra::OptRef<const Dating> dating
) noexcept {
    return dated_(raw, now, dating);
}

inline FrameReference::Step FrameReference::sample(VsyncSample s, Instant now) noexcept {
    const std::optional<Instant> prev_read = read_;
    const Step step = fold_(Wrapping::of(s.seq), now);
    if (!step.moved())
        return step;
    close_(std::min(now, std::max(prev_read.value_or(s.edge), s.edge)));
    return step;
}

inline std::chrono::nanoseconds FrameReference::lapse_since_look() noexcept {
    const std::chrono::nanoseconds gap = edge_ - prev_look_;
    prev_look_ = edge_;
    return gap;
}

inline void FrameReference::close_(Instant edge) noexcept {
    edge_gap_ = edge - edge_;
    edge_ = edge;
}

}
