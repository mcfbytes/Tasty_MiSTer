// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/identity_record.h"

#include "app/remembered_path.h"
#include "app/session_bindings.h"
#include "cores/core_profile.h"
#include "cores/registry.h"
#include "infra/fixed_str.h"
#include "svc/joy_plan.h"

namespace mister::app {
namespace {

static_assert(
    SessionBindings::kIdentityNameCap == SessionIdentity::kMax,
    "the name T-RT latched and the name the cell holds clip at one width"
);

void copy_cstr(char (&dst)[SessionIdentity::kMax], std::string_view src) noexcept {
    const std::size_t n = src.size() < SessionIdentity::kMax ? src.size()
                                                             : SessionIdentity::kMax - 1;
    for (std::size_t i = 0; i < n; ++i)
        dst[i] = src[i];
    for (std::size_t i = n; i < SessionIdentity::kMax; ++i)
        dst[i] = '\0';
}

}

IdentityRecord build_identity_record(
    const IdentityRecord::Inputs& in,
    const MraFacts& facts
) noexcept {

    FixedStr<SessionBindings::kIdentityNameCap, StrFit::Clip> latched{};
    if (in.identity_bound)
        (void)latched.assign(in.fields[0]);
    const std::string_view name = latched.view();
    const std::string_view eff = effective_name(name, facts);
    const cores::CoreProfile& p = cores::profile_for(name);

    IdentityRecord out{};
    SessionIdentity& id = out.identity;
    if (in.identity_bound) {
        const svc::ButtonLists lists = svc::choose_button_lists(
            in.button_override.names,
            in.button_override.defaults,
            in.fields[1],
            in.fields[2],
            in.fields[3]
        );
        id.joy = svc::build_joy_plan(lists.j, lists.jn, lists.jp, in.gamepad_defaults);
        (void)id.j_names.assign(lists.j);
    }
    id.joy.stick_dpad = !p.suppress_stick_dpad;
    copy_cstr(id.core, eff);
    copy_cstr(id.rbf, name);
    id.stem = RememberedStem::of(name, facts);
    id.setname_same_dir = facts.setname_same_dir;
    id.front_end = p.is_front_end;
    id.suppress_analog_followup = p.suppress_analog_followup;
    id.analog_reshape = p.analog_reshape;
    id.cue_dir = p.cue_browse_dir;
    id.cheats = infra::OptRef<const cores::CheatLookup>{p.cheats};
    id.slots = p.slots;
    id.image_no_zip = p.image_rows_no_zip;
    id.game_id = game_id_of(facts, in.log_file_entry);
    id.pause = p.host.pause;

    out.mgl_playable = !p.is_front_end && !p.suppresses_mgl;
    out.uart_default_baud = cores::profile_for(eff).default_uart_baud;
    out.stamp = in.stamp;
    return out;
}

}
