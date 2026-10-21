// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

#include "app/ui_request.h"
#include "infra/message_sum.h"

namespace mister::app {

struct UiAdmission {

    enum class Scope : std::uint8_t { None, Session, LiveSession };

    enum class Refusal : std::uint8_t { Answer, FileAsk };

    UiRequest::Kind kind;
    bool gate_switch = false;
    bool gate_load = false;
    Scope scoped = Scope::None;
    Refusal refuse_via = Refusal::Answer;
    bool bespoke = false;
};

inline constexpr auto kUiAdmission = [] {
    using K = UiRequest::Kind;
    constexpr auto kSession = UiAdmission::Scope::Session;
    constexpr auto kLive = UiAdmission::Scope::LiveSession;
    return std::to_array<UiAdmission>({
        {.kind = K::LoadCore},
        {.kind = K::SaveConfig},
        {.kind = K::SaveDips, .gate_switch = true, .gate_load = true},
        {.kind = K::SaveCoreConfig, .gate_switch = true, .gate_load = true, .scoped = kSession},
        {.kind = K::LoadCoreConfig, .gate_switch = true, .gate_load = true, .scoped = kSession},
        {.kind = K::LoadFile, .gate_switch = true, .gate_load = true},
        {.kind = K::MountImage, .gate_switch = true, .gate_load = true, .scoped = kSession},
        {.kind = K::UnmountImage, .gate_switch = true, .gate_load = true, .scoped = kSession},
        {.kind = K::ResetCore, .bespoke = true},
        {.kind = K::Reboot},
        {.kind = K::LoadFileByDigit, .gate_switch = true, .gate_load = true},
        {.kind = K::LoadRamImage,
         .gate_switch = true,
         .gate_load = true,
         .scoped = kLive,
         .refuse_via = UiAdmission::Refusal::FileAsk},
        {.kind = K::SetOsdStyle},
        {.kind = K::SdramDetected},
    });
}();
static_assert(
    infra::rows_are_ordinal(kUiAdmission),
    "kUiAdmission is one row per UiRequest kind, in ordinal order"
);

consteval bool bespoke_rows_are_bare(const decltype(kUiAdmission)& rows) {
    for (const UiAdmission& r : rows)
        if (r.bespoke
            && (r.gate_switch
                || r.gate_load
                || r.scoped != UiAdmission::Scope::None
                || r.refuse_via != UiAdmission::Refusal::Answer))
            return false;
    return true;
}

static_assert(bespoke_rows_are_bare(kUiAdmission), "a bespoke row's columns would be read as law");

consteval bool unscoped_rows_name_no_refusal(const decltype(kUiAdmission)& rows) {
    for (const UiAdmission& r : rows)
        if (r.scoped == UiAdmission::Scope::None && r.refuse_via != UiAdmission::Refusal::Answer)
            return false;
    return true;
}

static_assert(
    unscoped_rows_name_no_refusal(kUiAdmission),
    "refuse_via answers the scope test; a row without one has nothing to answer"
);

template <class A>
[[nodiscard]] consteval UiAdmission ui_admission() {
    return kUiAdmission[infra::ordinal(A::kKind)];
}

}
