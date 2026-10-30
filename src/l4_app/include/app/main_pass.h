// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mister::app {

struct MainPassStep {
    enum class Step : std::uint8_t {
        Reap,
        Reboot,
        LastOrder,
        HeldLoad,
        Belt,
        ParkAcks,
        Program,
        StartBound,
        Bridge,
        FrontEnd,
        LauncherSwap,
        Readiness,
        Fabric,
        kCount,
    };
    Step step;
    std::string_view reason;
};

inline constexpr std::array kMainPassSteps{
    MainPassStep{
        MainPassStep::Step::Reap,
        "a bay slot T-RT let go of is freed on the seat that built it"
    },
    MainPassStep{
        MainPassStep::Step::Reboot,
        "an owed reboot is the pass's whole work, as stock's blocked menu thread; "
        "the fabric facts still go out"
    },
    MainPassStep{
        MainPassStep::Step::LastOrder,
        "a refused last order first: the belt and the seats wait on it"
    },
    MainPassStep{
        MainPassStep::Step::HeldLoad,
        "then a held load, before the readiness watch: a user's switch makes an owed "
        "recovery moot"
    },
    MainPassStep{
        MainPassStep::Step::Belt,
        "one rung per pass, after every ring this pass drained: a mount that arrived "
        "this pass is armed by its pop and takes its first rung here"
    },
    MainPassStep{
        MainPassStep::Step::ParkAcks,
        "the park answer comes back rounds after the ask, so it is reaped on every "
        "pass and never inside the arm that asked"
    },
    MainPassStep{
        MainPassStep::Step::Program,
        "a reset that did not take ends a standing switch unprogrammed, as a refusal "
        "does; else the barrier opens on the acks just taken and the program runs"
    },
    MainPassStep{
        MainPassStep::Step::StartBound,
        "then the start's bound, after the held load: a later ask wins"
    },
    MainPassStep{
        MainPassStep::Step::Bridge,
        "SPI never serves a bridge slot, so an unbound one would wait for ever: MAIN "
        "ends the session"
    },
    MainPassStep{
        MainPassStep::Step::FrontEnd,
        "an owed front end, after the held load: a later ask wins"
    },
    MainPassStep{
        MainPassStep::Step::LauncherSwap,
        "the launcher's image swap is asked as a LoadCore, after the front end; a "
        "reboot owed spends it unasked"
    },
    MainPassStep{
        MainPassStep::Step::Readiness,
        "the readiness level is read on every pass too: the recovery decision is this "
        "seat's, with a recovery owed behind a switch"
    },
    MainPassStep{
        MainPassStep::Step::Fabric,
        "the fabric facts go out on every pass, the empty ones included: MAIN's 1 Hz "
        "floor is what keeps the cell fresh"
    },
};

consteval std::size_t pass_position(
    const std::array<MainPassStep, kMainPassSteps.size()>& pass,
    MainPassStep::Step s
) {
    for (std::size_t i = 0; i < pass.size(); ++i) {
        if (pass[i].step == s)
            return i;
    }
    return pass.size();
}

consteval bool main_pass_order_holds(const std::array<MainPassStep, kMainPassSteps.size()>& pass) {
    using S = MainPassStep::Step;
    constexpr std::array kStated{
        S::Reap,
        S::Reboot,
        S::LastOrder,
        S::HeldLoad,
        S::Belt,
        S::ParkAcks,
        S::Program,
        S::StartBound,
        S::Bridge,
        S::FrontEnd,
        S::LauncherSwap,
        S::Readiness,
        S::Fabric
    };
    static_assert(kStated.size() == static_cast<std::size_t>(S::kCount));
    if (pass.size() != kStated.size())
        return false;
    std::array<int, kStated.size()> seen{};
    for (const MainPassStep& p : pass) {
        if (p.step == S::kCount || p.reason.empty())
            return false;
        ++seen[static_cast<std::size_t>(p.step)];
    }
    for (const int n : seen) {
        if (n != 1)
            return false;
    }
    for (std::size_t i = 0; i + 1 < kStated.size(); ++i) {
        if (pass_position(pass, kStated[i]) >= pass_position(pass, kStated[i + 1]))
            return false;
    }
    return true;
}

static_assert(main_pass_order_holds(kMainPassSteps));

}
