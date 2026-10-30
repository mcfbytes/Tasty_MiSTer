// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "infra/error.h"
#include "infra/seat.h"
#include "os/deadline.h"
#include "proto/types.h"

namespace mister::app::phase {

struct Gen {
    std::uint32_t v = 0;
    friend constexpr bool operator==(Gen, Gen) = default;
};

struct Idle {};

struct Asked {
    Gen gen;
};

struct Parking {
    Gen gen;
};

struct Programming {
    Gen gen;
    bool loaded = false;
};

struct AwaitIdentity {
    Gen gen;
    std::uint32_t tries = 0;
    std::optional<os::Deadline> bound{};
};

struct AwaitConfStr {
    Gen gen;
    bool loaded = false;
    std::optional<os::Deadline> bound{};
};

struct Building {
    Gen gen;
    proto::BindGeneration conf_gen;
    std::optional<os::Deadline> bound{};
};

struct Tail {
    Gen gen;
    proto::BindGeneration conf_gen;
};

struct Up {
    Gen gen;
    proto::BindGeneration conf_gen;
};

struct Refused {
    Gen gen;
    Errc why = Errc::negotiation;
    bool front_end = false;
    bool by_bridge = false;
};

using Stage = std::variant<
    Idle,
    Asked,
    Parking,
    Programming,
    AwaitIdentity,
    AwaitConfStr,
    Building,
    Tail,
    Up,
    Refused>;

struct Recovering {
    Gen gen;
    Stage under{};
};

struct Rebooting {
    bool cold = true;
    Stage under{};
};

using Arm = std::variant<
    Idle,
    Asked,
    Parking,
    Programming,
    AwaitIdentity,
    AwaitConfStr,
    Building,
    Tail,
    Up,
    Refused,
    Recovering,
    Rebooting>;

enum class PhaseKind : std::uint8_t {
    Idle,
    Asked,
    Parking,
    Programming,
    AwaitIdentity,
    AwaitConfStr,
    Building,
    Tail,
    Up,
    Refused,
    Recovering,
    Rebooting,
    kCount
};
inline constexpr std::size_t kPhases = static_cast<std::size_t>(PhaseKind::kCount);
static_assert(std::variant_size_v<Arm> == kPhases);

template <PhaseKind K, class T>
inline constexpr bool
    kArmIs = std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(K), Arm>, T>;
static_assert(
    kArmIs<PhaseKind::Idle, Idle>
    && kArmIs<PhaseKind::Asked, Asked>
    && kArmIs<PhaseKind::Parking, Parking>
    && kArmIs<PhaseKind::Programming, Programming>
    && kArmIs<PhaseKind::AwaitIdentity, AwaitIdentity>
    && kArmIs<PhaseKind::AwaitConfStr, AwaitConfStr>
    && kArmIs<PhaseKind::Building, Building>
    && kArmIs<PhaseKind::Tail, Tail>
    && kArmIs<PhaseKind::Up, Up>
    && kArmIs<PhaseKind::Refused, Refused>
    && kArmIs<PhaseKind::Recovering, Recovering>
    && kArmIs<PhaseKind::Rebooting, Rebooting>
);

[[nodiscard]] constexpr PhaseKind kind(const Arm& p) noexcept {
    return static_cast<PhaseKind>(p.index());
}

inline constexpr std::size_t kStages = std::variant_size_v<Stage>;
static_assert(kStages == static_cast<std::size_t>(PhaseKind::Recovering));

[[nodiscard]] constexpr PhaseKind kind(const Stage& s) noexcept {
    return static_cast<PhaseKind>(s.index());
}
template <class T>
inline constexpr bool kOverlay = std::is_same_v<T, Recovering> || std::is_same_v<T, Rebooting>;

inline constexpr auto kPhaseNames = std::to_array<std::string_view>(
    {"Idle",
     "Asked",
     "Parking",
     "Programming",
     "AwaitIdentity",
     "AwaitConfStr",
     "Building",
     "Tail",
     "Up",
     "Refused",
     "Recovering",
     "Rebooting"}
);
static_assert(kPhaseNames.size() == kPhases);

enum class Stimulus : std::uint8_t {
    LoadCore,
    FrontEnd,
    RecoveryAsk,
    ParkPushed,
    ParkComplete,
    PauseExpired,
    ProgramOk,
    ProgramFailed,
    BootLoad,
    BootApply,
    BootAdopt,
    IdentityMatched,
    IdentitySpent,
    AcceptRefused,
    ConfStrRefused,
    ConfStrAccepted,
    ConfStrUnawaited,
    MakeRefused,
    CoreRefused,
    CoreMade,
    ManifestFailed,
    SessionUp,
    StartRefused,
    BoundExpired,
    BridgeEnded,
    ReadyLost,
    ProbeOk,
    ProbeOkUnasked,
    ProbeOkRefused,
    ProbeOkAwaiting,
    ProbeOkBuilding,
    ProbeExhausted,
    Reboot,
    RebootQuiesced,
    PauseWedged,
    kCount
};
inline constexpr std::size_t kStimuli = static_cast<std::size_t>(Stimulus::kCount);

inline constexpr auto kStimulusNames = std::to_array<std::string_view>(
    {"LoadCore",        "FrontEnd",         "RecoveryAsk",    "ParkPushed",     "ParkComplete",
     "PauseExpired",    "ProgramOk",        "ProgramFailed",  "BootLoad",       "BootApply",
     "BootAdopt",       "IdentityMatched",  "IdentitySpent",  "AcceptRefused",  "ConfStrRefused",
     "ConfStrAccepted", "ConfStrUnawaited", "MakeRefused",    "CoreRefused",    "CoreMade",
     "ManifestFailed",  "SessionUp",        "StartRefused",   "BoundExpired",   "BridgeEnded",
     "ReadyLost",       "ProbeOk",          "ProbeOkUnasked", "ProbeOkRefused", "ProbeOkAwaiting",
     "ProbeOkBuilding", "ProbeExhausted",   "Reboot",         "RebootQuiesced", "PauseWedged"}
);
static_assert(kStimulusNames.size() == kStimuli);

enum class Origin : std::uint8_t { Design, Tree, Tolerance, Refusal };

struct PhaseSet {
    std::uint16_t bits = 0;

    [[nodiscard]] constexpr bool has(PhaseKind k) const noexcept {
        return (bits & (1u << static_cast<unsigned>(k))) != 0;
    }
};

static_assert(kPhases <= 16, "one bit per phase");

[[nodiscard]] consteval PhaseSet of(std::initializer_list<PhaseKind> ks) {
    PhaseSet s{};
    for (const PhaseKind k : ks)
        s.bits = static_cast<std::uint16_t>(s.bits | (1u << static_cast<unsigned>(k)));
    return s;
}

inline constexpr PhaseSet kAnyPhase{static_cast<std::uint16_t>((1u << kPhases) - 1u)};

[[nodiscard]] consteval PhaseSet all_but(PhaseSet s) {
    return PhaseSet{static_cast<std::uint16_t>(kAnyPhase.bits & ~s.bits)};
}

struct Row {
    PhaseSet from;
    Stimulus on;
    std::optional<PhaseKind> to;
    Origin origin;
};

inline constexpr std::optional<PhaseKind> kHold = std::nullopt;

inline constexpr auto kTransitions = [] {
    using K = PhaseKind;
    using S = Stimulus;
    using O = Origin;
    constexpr PhaseSet rc = of({K::Recovering});
    constexpr PhaseSet rb = of({K::Rebooting});
    constexpr PhaseSet ov = of({K::Recovering, K::Rebooting});
    return std::to_array<Row>({
        {of({K::Idle, K::Up}), S::LoadCore, K::Asked, O::Design},
        {of({K::AwaitConfStr, K::Building, K::Tail, K::Refused}), S::LoadCore, K::Asked, O::Design},
        {rc, S::LoadCore, kHold, O::Tolerance},
        {of({K::Asked, K::Parking, K::Programming, K::AwaitIdentity, K::Rebooting}),
         S::LoadCore,
         kHold,
         O::Refusal},

        {of({K::Refused}), S::FrontEnd, K::Asked, O::Design},
        {rc, S::FrontEnd, kHold, O::Tolerance},
        {all_but(of({K::Refused, K::Recovering})), S::FrontEnd, kHold, O::Refusal},

        {rc, S::RecoveryAsk, K::Recovering, O::Tree},
        {all_but(rc), S::RecoveryAsk, kHold, O::Refusal},

        {of({K::Asked}), S::ParkPushed, K::Parking, O::Design},
        {rc, S::ParkPushed, kHold, O::Tree},
        {all_but(of({K::Asked, K::Recovering})), S::ParkPushed, kHold, O::Refusal},

        {of({K::Parking}), S::ParkComplete, K::Programming, O::Design},
        {rc, S::ParkComplete, kHold, O::Tolerance},
        {all_but(of({K::Parking, K::Recovering})), S::ParkComplete, kHold, O::Refusal},

        {of({K::Parking}), S::PauseExpired, K::Programming, O::Design},
        {rc, S::PauseExpired, kHold, O::Tolerance},
        {all_but(of({K::Parking, K::Recovering})), S::PauseExpired, kHold, O::Refusal},

        {of({K::Parking}), S::PauseWedged, K::Rebooting, O::Design},
        {rc, S::PauseWedged, kHold, O::Tolerance},
        {all_but(of({K::Parking, K::Recovering})), S::PauseWedged, kHold, O::Refusal},

        {of({K::Programming}), S::ProgramOk, K::AwaitIdentity, O::Design},
        {rc, S::ProgramOk, kHold, O::Tolerance},
        {all_but(of({K::Programming, K::Recovering})), S::ProgramOk, kHold, O::Refusal},

        {of({K::Programming}), S::ProgramFailed, K::AwaitConfStr, O::Tree},
        {rc, S::ProgramFailed, kHold, O::Tolerance},
        {all_but(of({K::Programming, K::Recovering})), S::ProgramFailed, kHold, O::Refusal},

        {of({K::Idle}), S::BootLoad, K::Programming, O::Design},
        {ov, S::BootLoad, kHold, O::Tolerance},
        {all_but(of({K::Idle, K::Recovering, K::Rebooting})), S::BootLoad, kHold, O::Refusal},

        {of({K::Programming}), S::BootApply, K::AwaitConfStr, O::Tree},
        {ov, S::BootApply, kHold, O::Tolerance},
        {all_but(of({K::Programming, K::Recovering, K::Rebooting})),
         S::BootApply,
         kHold,
         O::Refusal},

        {of({K::Idle}), S::BootAdopt, K::AwaitConfStr, O::Tree},
        {ov, S::BootAdopt, kHold, O::Tolerance},
        {all_but(of({K::Idle, K::Recovering, K::Rebooting})), S::BootAdopt, kHold, O::Refusal},

        {of({K::AwaitIdentity}), S::IdentityMatched, K::AwaitConfStr, O::Design},
        {rc, S::IdentityMatched, kHold, O::Tolerance},
        {all_but(of({K::AwaitIdentity, K::Recovering})), S::IdentityMatched, kHold, O::Refusal},

        {of({K::AwaitIdentity}), S::IdentitySpent, K::Refused, O::Design},
        {rc, S::IdentitySpent, kHold, O::Tolerance},
        {all_but(of({K::AwaitIdentity, K::Recovering})), S::IdentitySpent, kHold, O::Refusal},

        {of({K::AwaitConfStr}), S::AcceptRefused, K::Refused, O::Tree},
        {rc, S::AcceptRefused, kHold, O::Design},
        {all_but(of({K::AwaitConfStr, K::Recovering})), S::AcceptRefused, kHold, O::Refusal},

        {of({K::AwaitConfStr}), S::ConfStrRefused, K::Refused, O::Tree},
        {rc, S::ConfStrRefused, kHold, O::Design},
        {all_but(of({K::AwaitConfStr, K::Recovering})), S::ConfStrRefused, kHold, O::Refusal},

        {of({K::AwaitConfStr}), S::ConfStrAccepted, K::Building, O::Design},
        {rc, S::ConfStrAccepted, kHold, O::Design},
        {all_but(of({K::AwaitConfStr, K::Recovering})), S::ConfStrAccepted, kHold, O::Refusal},

        {of({K::Idle, K::AwaitConfStr}), S::ConfStrUnawaited, K::Building, O::Tolerance},
        {of({K::AwaitIdentity, K::Building, K::Tail, K::Up, K::Recovering}),
         S::ConfStrUnawaited,
         kHold,
         O::Tolerance},
        {of({K::Asked, K::Parking, K::Programming, K::Refused, K::Rebooting}),
         S::ConfStrUnawaited,
         kHold,
         O::Refusal},

        {of({K::Building}), S::MakeRefused, K::Refused, O::Tree},
        {of({K::AwaitIdentity, K::Tail, K::Up}), S::MakeRefused, K::Refused, O::Tolerance},
        {rc, S::MakeRefused, kHold, O::Design},
        {all_but(of({K::Building, K::AwaitIdentity, K::Tail, K::Up, K::Recovering})),
         S::MakeRefused,
         kHold,
         O::Refusal},

        {of({K::Building}), S::CoreRefused, K::Refused, O::Tree},
        {of({K::Idle, K::AwaitIdentity, K::AwaitConfStr, K::Tail, K::Up}),
         S::CoreRefused,
         K::Refused,
         O::Tolerance},
        {rc, S::CoreRefused, kHold, O::Design},
        {of({K::Asked, K::Parking, K::Programming, K::Refused, K::Rebooting}),
         S::CoreRefused,
         kHold,
         O::Refusal},

        {of({K::Building}), S::CoreMade, K::Tail, O::Design},
        {of({K::Idle, K::Up}), S::CoreMade, K::Tail, O::Tolerance},
        {of({K::AwaitIdentity, K::AwaitConfStr, K::Tail}), S::CoreMade, kHold, O::Tolerance},
        {rc, S::CoreMade, kHold, O::Design},
        {of({K::Asked, K::Parking, K::Programming, K::Refused, K::Rebooting}),
         S::CoreMade,
         kHold,
         O::Refusal},

        {of({K::Tail}), S::ManifestFailed, K::Refused, O::Tree},
        {rc, S::ManifestFailed, kHold, O::Tolerance},
        {all_but(of({K::Tail, K::Recovering})), S::ManifestFailed, kHold, O::Refusal},

        {of({K::Tail}), S::SessionUp, K::Up, O::Design},
        {of({K::AwaitIdentity, K::AwaitConfStr, K::Up}), S::SessionUp, kHold, O::Tolerance},
        {rc, S::SessionUp, kHold, O::Design},
        {of({K::Idle, K::Asked, K::Parking, K::Programming, K::Building, K::Refused, K::Rebooting}),
         S::SessionUp,
         kHold,
         O::Refusal},

        {of({K::AwaitIdentity, K::AwaitConfStr, K::Building, K::Tail}),
         S::StartRefused,
         K::Refused,
         O::Design},
        {of({K::Up}), S::StartRefused, K::Refused, O::Design},
        {of({K::Idle, K::Parking}), S::StartRefused, K::Refused, O::Tolerance},
        {rc, S::StartRefused, kHold, O::Design},
        {of({K::Asked, K::Programming, K::Refused, K::Rebooting}),
         S::StartRefused,
         kHold,
         O::Refusal},

        {of({K::AwaitIdentity, K::AwaitConfStr}), S::BoundExpired, K::Refused, O::Design},
        {of({K::Building}), S::BoundExpired, K::Refused, O::Tree},
        {rc, S::BoundExpired, kHold, O::Design},
        {all_but(of({K::AwaitIdentity, K::AwaitConfStr, K::Building, K::Recovering})),
         S::BoundExpired,
         kHold,
         O::Refusal},

        {of({K::Idle, K::AwaitConfStr, K::Building, K::Tail, K::Up}),
         S::BridgeEnded,
         K::Refused,
         O::Design},
        {rc, S::BridgeEnded, kHold, O::Tolerance},
        {of({K::Asked, K::Parking, K::Programming, K::AwaitIdentity, K::Refused, K::Rebooting}),
         S::BridgeEnded,
         kHold,
         O::Refusal},

        {of({K::Up}), S::ReadyLost, K::Recovering, O::Design},
        {of({K::Idle, K::AwaitConfStr, K::Building, K::Tail, K::Refused}),
         S::ReadyLost,
         K::Recovering,
         O::Design},
        {of({K::Asked, K::Parking, K::Programming, K::AwaitIdentity, K::Recovering, K::Rebooting}),
         S::ReadyLost,
         kHold,
         O::Refusal},

        {of({K::Recovering}), S::ProbeOk, K::Parking, O::Tree},
        {all_but(of({K::Recovering})), S::ProbeOk, kHold, O::Refusal},

        {of({K::Recovering}), S::ProbeOkUnasked, K::Idle, O::Tree},
        {all_but(of({K::Recovering})), S::ProbeOkUnasked, kHold, O::Refusal},

        {of({K::Recovering}), S::ProbeOkRefused, K::Refused, O::Tree},
        {all_but(of({K::Recovering})), S::ProbeOkRefused, kHold, O::Refusal},

        {of({K::Recovering}), S::ProbeOkAwaiting, K::AwaitConfStr, O::Tree},
        {all_but(of({K::Recovering})), S::ProbeOkAwaiting, kHold, O::Refusal},

        {of({K::Recovering}), S::ProbeOkBuilding, K::Building, O::Tree},
        {all_but(of({K::Recovering})), S::ProbeOkBuilding, kHold, O::Refusal},

        {of({K::Recovering}), S::ProbeExhausted, K::Rebooting, O::Design},
        {rb, S::ProbeExhausted, K::Rebooting, O::Tolerance},
        {all_but(of({K::Recovering, K::Rebooting})), S::ProbeExhausted, kHold, O::Refusal},

        {kAnyPhase, S::Reboot, K::Rebooting, O::Design},

        {rb, S::RebootQuiesced, kHold, O::Design},
        {all_but(rb), S::RebootQuiesced, kHold, O::Refusal},
    });
}();

template <std::size_t N>
[[nodiscard]] consteval bool covers_each_pair_once(const std::array<Row, N>& rows) {
    std::array<std::array<unsigned, kStimuli>, kPhases> seen{};
    for (const Row& r : rows) {
        if (r.from.bits == 0 || r.on >= Stimulus::kCount)
            return false;
        if (r.origin == Origin::Refusal && r.to.has_value())
            return false;
        if (r.to && *r.to >= PhaseKind::kCount)
            return false;
        for (std::size_t k = 0; k < kPhases; ++k) {
            if (r.from.has(static_cast<PhaseKind>(k)))
                ++seen[k][static_cast<std::size_t>(r.on)];
        }
    }
    for (const auto& per_phase : seen) {
        for (const unsigned n : per_phase) {
            if (n != 1)
                return false;
        }
    }
    return true;
}

static_assert(
    covers_each_pair_once(kTransitions),
    "kTransitions names every (phase, stimulus) pair exactly once - a pair with no row "
    "has no defined answer, a pair with two has two"
);

[[nodiscard]] constexpr std::size_t row_index(PhaseKind k, Stimulus s) noexcept {
    for (std::size_t i = 0; i < kTransitions.size(); ++i) {
        if (kTransitions[i].on == s && kTransitions[i].from.has(k))
            return i;
    }
    return kTransitions.size();
}

struct Facts {
    Gen gen;
    bool loaded = false;
    proto::BindGeneration conf_gen{};
    Errc why = Errc::negotiation;
    bool cold = true;
    std::optional<os::Deadline> bound{};
    bool front_end = false;
    bool by_bridge = false;
};

template <class T>
[[nodiscard]] constexpr T arm_of(const Facts& f) noexcept {
    T a{};
    if constexpr (requires { a.gen; })
        a.gen = f.gen;
    if constexpr (requires { a.loaded; })
        a.loaded = f.loaded;
    if constexpr (requires { a.conf_gen; })
        a.conf_gen = f.conf_gen;
    if constexpr (requires { a.why; })
        a.why = f.why;
    if constexpr (requires { a.cold; })
        a.cold = f.cold;
    if constexpr (requires { a.bound; })
        a.bound = f.bound;
    if constexpr (requires { a.front_end; })
        a.front_end = f.front_end;
    if constexpr (requires { a.by_bridge; })
        a.by_bridge = f.by_bridge;
    return a;
}

template <std::size_t... I>
[[nodiscard]] constexpr Arm make_indexed(
    PhaseKind k,
    const Facts& f,
    std::index_sequence<I...>
) noexcept {
    Arm out{};
    ((static_cast<std::size_t>(k) == I ? (void)(out = arm_of<std::variant_alternative_t<I, Arm>>(f))
                                       : (void)0),
     ...);
    return out;
}

[[nodiscard]] constexpr Arm make(PhaseKind k, const Facts& f) noexcept {
    return make_indexed(k, f, std::make_index_sequence<kPhases>{});
}

[[nodiscard]] constexpr std::optional<Gen> gen_of(const Arm& p) noexcept {
    return std::visit(
        [](const auto& a) -> std::optional<Gen> {
            if constexpr (requires { a.gen; }) {
                return a.gen;
            } else {
                return std::nullopt;
            }
        },
        p
    );
}

[[nodiscard]] constexpr Arm arm_of_stage(const Stage& s) noexcept {
    return std::visit([](const auto& a) -> Arm { return a; }, s);
}

[[nodiscard]] constexpr Stage stage_of(const Arm& p) noexcept {
    return std::visit(
        [](const auto& a) -> Stage {
            if constexpr (kOverlay<std::decay_t<decltype(a)>>) {
                return a.under;
            } else {
                return a;
            }
        },
        p
    );
}

struct Step {
    Arm next;
    std::size_t row = 0;
};

[[nodiscard]] constexpr Step take_row(const Arm& now, Stimulus s, const Facts& f) noexcept {
    const std::size_t i = row_index(kind(now), s);
    const Row& r = kTransitions[i];
    if (!r.to)
        return Step{.next = now, .row = i};
    Arm next = make(*r.to, f);
    std::visit(
        [](const auto& from, auto& to) {
            if constexpr (requires { from.bound; } && requires { to.bound; })
                to.bound = from.bound;
        },
        now,
        next
    );
    return Step{.next = next, .row = i};
}

[[nodiscard]] constexpr bool overlay_own(Stimulus on) noexcept {
    using S = Stimulus;
    return on == S::ReadyLost
        || on == S::ProbeOk
        || on == S::ProbeOkUnasked
        || on == S::ProbeOkRefused
        || on == S::ProbeOkAwaiting
        || on == S::ProbeOkBuilding
        || on == S::ProbeExhausted
        || on == S::Reboot
        || on == S::RebootQuiesced;
}

[[nodiscard]] constexpr Stage stage_after(const Stage& s, Stimulus on, const Facts& f) noexcept {
    using S = Stimulus;
    if (overlay_own(on))
        return s;
    const Arm next = take_row(arm_of_stage(s), on == S::RecoveryAsk ? S::LoadCore : on, f).next;
    if (kind(next) >= PhaseKind::Recovering)
        return s;
    return stage_of(next);
}

[[nodiscard]] constexpr Step advance(const Arm& now, Stimulus s, const Facts& f) noexcept {
    Step step = take_row(now, s, f);
    const bool moves = kind(now) >= PhaseKind::Recovering
        && kTransitions[step.row].origin != Origin::Refusal;
    const Stage stage = moves ? stage_after(stage_of(now), s, f) : stage_of(now);
    const bool was_overlay = kind(now) >= PhaseKind::Recovering;
    std::visit(
        [&](auto& a) {
            if constexpr (kOverlay<std::decay_t<decltype(a)>>)
                a.under = stage;
        },
        step.next
    );
    if (was_overlay && kind(step.next) == kind(stage))
        step.next = arm_of_stage(stage);
    return step;
}

[[nodiscard]] constexpr Origin judged(const Arm& now, Stimulus on) noexcept {
    const Origin row = kTransitions[row_index(kind(now), on)].origin;
    if (kind(now) < PhaseKind::Recovering || overlay_own(on) || row == Origin::Refusal)
        return row;
    const Stimulus as = on == Stimulus::RecoveryAsk ? Stimulus::LoadCore : on;
    const Origin stage = kTransitions[row_index(kind(stage_of(now)), as)].origin;
    return stage > row ? stage : row;
}

static_assert(
    Origin::Design < Origin::Tree
    && Origin::Tree < Origin::Tolerance
    && Origin::Tolerance < Origin::Refusal
);

[[nodiscard]] constexpr Arm over(PhaseKind o, const Stage& under, const Facts& f) noexcept {
    Arm a = make(o, f);
    std::visit(
        [&](auto& v) {
            if constexpr (kOverlay<std::decay_t<decltype(v)>>)
                v.under = under;
        },
        a
    );
    return a;
}

[[nodiscard]] consteval bool overlay_moves_stage_by_rows(std::size_t pair) {
    const PhaseKind o = pair < kStages ? PhaseKind::Recovering : PhaseKind::Rebooting;
    const Facts f{.gen = Gen{1}, .loaded = true};
    Facts own = f;
    own.gen = Gen{2};
    const Stage stage = stage_of(make(static_cast<PhaseKind>(pair % kStages), own));
    const PhaseKind was = kind(stage);
    for (std::size_t i = 0; i < kStimuli; ++i) {
        const auto on = static_cast<Stimulus>(i);
        const Step step = advance(over(o, stage, f), on, f);
        const Row& r = kTransitions[step.row];
        const bool still = kind(step.next) >= PhaseKind::Recovering;
        const PhaseKind now = kind(stage_of(step.next));
        if (overlay_own(on)) {
            if (still ? now != was : kind(step.next) != r.to.value_or(o))
                return false;
            if (!still && now == was && gen_of(step.next) != gen_of(arm_of_stage(stage)))
                return false;
            continue;
        }
        if (!still || r.to.value_or(o) != o)
            return false;
        if (r.origin == Origin::Refusal) {
            if (now != was)
                return false;
            continue;
        }
        const Stimulus as = on == Stimulus::RecoveryAsk ? Stimulus::LoadCore : on;
        const PhaseKind by_row = kind(take_row(arm_of_stage(stage), as, f).next);
        if (now != (by_row >= PhaseKind::Recovering ? was : by_row))
            return false;
    }
    return true;
}

template <std::size_t Pair>
inline constexpr bool kOverlayMovesByRows = overlay_moves_stage_by_rows(Pair);
static_assert(
    []<std::size_t... P>(std::index_sequence<P...>) {
        return (kOverlayMovesByRows<P> && ...);
    }(std::make_index_sequence<2 * kStages>{}),
    "under an overlay a stage moves by its own row, the overlay's own edges leave it be, "
    "and a refusal of the visible arm moves no stage"
);

[[nodiscard]] consteval bool rebooting_refuses_the_start() {
    using S = Stimulus;
    for (std::size_t i = 0; i < kStimuli; ++i) {
        const auto on = static_cast<S>(i);
        if (overlay_own(on) || on == S::BootLoad || on == S::BootApply || on == S::BootAdopt)
            continue;
        if (kTransitions[row_index(PhaseKind::Rebooting, on)].origin != Origin::Refusal)
            return false;
    }
    return kTransitions[row_index(PhaseKind::Rebooting, S::ReadyLost)].origin == Origin::Refusal;
}

static_assert(rebooting_refuses_the_start(), "a reboot owed refuses every start stimulus");

struct SwitchPhase {
    TASTY_SEAT_RESIDENT(Main);

    struct Tally {
        std::uint32_t tolerated = 0;
        std::uint32_t misrouted = 0;
        std::uint32_t refused = 0;
        std::array<std::uint32_t, kTransitions.size()> taken{};
    };

    Arm now{};
    Tally tally{};

    struct Taken {
        Origin origin = Origin::Design;
        PhaseKind at = PhaseKind::Idle;
        PhaseKind under = PhaseKind::Idle;
        std::uint32_t gen = 0;
    };

    constexpr Taken take(Stimulus on, const Facts& f) noexcept {
        const Taken t{
            .origin = judged(now, on),
            .at = kind(now),
            .under = kind(stage()),
            .gen = gen_of(now).value_or(Gen{}).v
        };
        const Step step = advance(now, on, f);
        now = step.next;
        ++tally.taken[step.row];
        if (t.origin == Origin::Tolerance)
            ++tally.tolerated;
        if (t.origin == Origin::Refusal)
            ++tally.misrouted;
        return t;
    }

    [[nodiscard]] constexpr bool refuse(Stimulus on) noexcept {
        const std::size_t i = row_index(kind(now), on);
        ++tally.taken[i];
        ++tally.refused;
        return kTransitions[i].origin == Origin::Refusal;
    }

    [[nodiscard]] constexpr Stage stage() const noexcept { return stage_of(now); }

    template <class T>
    [[nodiscard]] constexpr const T* in_stage() const noexcept {
        if (const auto* r = std::get_if<Recovering>(&now))
            return std::get_if<T>(&r->under);
        if (const auto* b = std::get_if<Rebooting>(&now))
            return std::get_if<T>(&b->under);
        return std::get_if<T>(&now);
    }

    [[nodiscard]] constexpr std::optional<os::Deadline> bound() const noexcept {
        std::optional<os::Deadline> out{};
        visit_stage_([&](const auto& a) {
            if constexpr (requires { a.bound; })
                out = a.bound;
        });
        return out;
    }

    constexpr void rebound(const os::Deadline& due) noexcept {
        visit_stage_([&](auto& a) {
            if constexpr (requires { a.bound; })
                if (a.bound)
                    a.bound = due;
        });
    }

    constexpr void end_bound() noexcept {
        visit_stage_([](auto& a) {
            if constexpr (requires { a.bound; })
                a.bound.reset();
        });
    }

    [[nodiscard]] constexpr bool spend_identity_try(std::uint32_t budget) noexcept {
        bool spent = false;
        visit_stage_([&](auto& a) {
            if constexpr (std::is_same_v<std::decay_t<decltype(a)>, AwaitIdentity>)
                spent = budget != 0 && ++a.tries > budget;
        });
        return spent;
    }

    [[nodiscard]] constexpr bool front_end_owed() const noexcept {
        const Refused* r = in_stage<Refused>();
        return r != nullptr && r->front_end;
    }

    constexpr void drop_front_end() noexcept {
        visit_stage_([](auto& a) {
            if constexpr (std::is_same_v<std::decay_t<decltype(a)>, Refused>)
                a.front_end = false;
        });
    }

    constexpr void wedge() noexcept {
        Stage s = stage();
        if (const auto* w = std::get_if<AwaitConfStr>(&s); w != nullptr && w->loaded)
            s = Idle{};
        put_stage_(s);
    }

    [[nodiscard]] constexpr std::uint32_t cores_made() const noexcept {
        std::uint32_t n = 0;
        for (std::size_t i = 0; i < kTransitions.size(); ++i)
            if (kTransitions[i].on == Stimulus::CoreMade)
                n += tally.taken[i];
        return n;
    }

private:
    template <class F>
    constexpr void visit_stage_(F&& f) const noexcept {
        const Stage s = stage();
        std::visit(f, s);
    }

    template <class F>
    constexpr void visit_stage_(F&& f) noexcept {
        Stage s = stage();
        std::visit(f, s);
        put_stage_(s);
    }

    constexpr void put_stage_(const Stage& s) noexcept {
        if (auto* r = std::get_if<Recovering>(&now)) {
            r->under = s;
        } else if (auto* b = std::get_if<Rebooting>(&now)) {
            b->under = s;
        } else {
            now = arm_of_stage(s);
        }
    }
};

[[nodiscard]] consteval bool wedge_rests_only_a_loaded_wait() {
    for (const bool loaded : {false, true}) {
        const Facts f{.gen = Gen{1}, .loaded = loaded};
        for (std::size_t st = 0; st < kStages; ++st) {
            const auto k = static_cast<PhaseKind>(st);
            const PhaseKind rest = k == PhaseKind::AwaitConfStr && loaded ? PhaseKind::Idle : k;
            for (const PhaseKind o : {k, PhaseKind::Recovering, PhaseKind::Rebooting}) {
                SwitchPhase t{.now = o == k ? make(k, f) : over(o, stage_of(make(k, f)), f)};
                t.wedge();
                if (kind(t.now) != (o == k ? rest : o) || kind(t.stage()) != rest)
                    return false;
            }
        }
    }
    return true;
}

static_assert(wedge_rests_only_a_loaded_wait(), "a wedge moves only a loaded CONF_STR wait");

}
