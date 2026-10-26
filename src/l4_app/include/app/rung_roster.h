// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include "app/rung.h"
#include "app/rung_edges.h"
#include "cores/ladder_host.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::app {

enum class RungSlot : std::uint8_t {
    BootLadder,
    SavestateHold,
    SaveMount,
    AddonsAfter,
    Addons,
    Watch,
    Pieces,
    Load,
    Manifest,
    Walk,
    Companion,
    kCount
};

enum class RungSource : std::uint8_t { Role, Held };

struct RungRow {
    RungSlot slot;
    RungSource source;
};

inline constexpr auto kRungRows = std::to_array<RungRow>({
    {RungSlot::BootLadder, RungSource::Held},
    {RungSlot::SavestateHold, RungSource::Held},
    {RungSlot::SaveMount, RungSource::Held},
    {RungSlot::AddonsAfter, RungSource::Held},
    {RungSlot::Addons, RungSource::Role},
    {RungSlot::Watch, RungSource::Held},
    {RungSlot::Pieces, RungSource::Role},
    {RungSlot::Load, RungSource::Role},
    {RungSlot::Manifest, RungSource::Role},
    {RungSlot::Walk, RungSource::Role},
    {RungSlot::Companion, RungSource::Role},
});

consteval bool rung_rows_cover_each_slot_once() {
    unsigned seen[static_cast<std::size_t>(RungSlot::kCount)] = {};
    for (const RungRow& r : kRungRows)
        ++seen[static_cast<std::size_t>(r.slot)];
    for (unsigned n : seen) {
        if (n != 1)
            return false;
    }
    return true;
}

static_assert(
    rung_rows_cover_each_slot_once(),
    "kRungRows declares each rung source exactly once - a slot with no row is "
    "never stepped, a slot with two is stepped twice a pass"
);

class RungRoster {
    TASTY_SEAT_RESIDENT(Main);

public:
    enum class Wait : std::uint8_t { None, Later, Now };

    class Host : public virtual cores::ILadderHost {
    public:
        [[nodiscard]] virtual infra::OptRef<Rung> rung(RungSlot slot) noexcept = 0;
        [[nodiscard]] virtual infra::OptRef<const Rung> rung(RungSlot slot) const noexcept = 0;

        virtual void settle(RungSlot slot, Rung::Pass pass) = 0;

        virtual void step_held(RungSlot slot) = 0;
        [[nodiscard]] virtual Wait held_wait(RungSlot slot) const noexcept = 0;

    protected:
        Host() = default;
        Host(const Host&) = default;
        Host& operator=(const Host&) = default;
        ~Host() override = default;
    };

    explicit RungRoster(const RungEdges& edges) noexcept : edges_(edges) {}

    void step_all(Host& host) {
        TASTY_SEAT_BODY(RungRoster);
        for (const RungRow& row : kRungRows) {
            switch (row.source) {
                case RungSource::Held:
                    host.step_held(row.slot);
                    break;
                case RungSource::Role: {

                    const infra::OptRef<Rung> live = host.rung(row.slot);
                    if (!live)
                        break;
                    host.settle(row.slot, live->step(edges_, host));
                    break;
                }
            }
        }
    }

    [[nodiscard]] Wait wait(const Host& host) const noexcept {
        TASTY_SEAT_BODY(RungRoster);
        Wait soonest = Wait::None;
        for (const RungRow& row : kRungRows) {
            Wait w = Wait::None;
            switch (row.source) {
                case RungSource::Held:
                    w = host.held_wait(row.slot);
                    break;
                case RungSource::Role:
                    if (const infra::OptRef<const Rung> live = host.rung(row.slot)) {
                        w = live->wants_pass(edges_.inbox, edges_.level) ? Wait::Now : Wait::Later;
                    }
                    break;
            }
            soonest = std::max(soonest, w);
        }
        return soonest;
    }

    [[nodiscard]] bool any_wants_pass(const Host& host) const noexcept {
        return wait(host) == Wait::Now;
    }

    [[nodiscard]] const RungEdges& edges() const noexcept { return edges_; }

private:
    const RungEdges edges_;
};

}
