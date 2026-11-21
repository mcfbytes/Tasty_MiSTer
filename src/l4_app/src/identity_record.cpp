// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/identity_record.h"

#include "app/core_name.h"
#include "app/remembered_path.h"
#include "cores/core_profile.h"
#include "cores/registry.h"
#include "svc/input/joy_plan.h"

namespace mister::app {

IdentityRecord build_identity_record(
    const IdentityRecord::Inputs& in,
    const MraFacts& facts
) noexcept {

    CoreName latched{};
    if (in.identity_bound)
        (void)latched.assign(in.fields[0]);
    const std::string_view name = latched.view();
    const std::string_view eff = effective_name(name, facts);
    const cores::CoreProfile& p = cores::profile_for(name);

    IdentityRecord out{};
    SessionIdentity& id = out.identity;
    if (in.identity_bound) {
        const svc::input::ButtonLists
            lists = svc::input::ButtonLists{in.fields[1], in.fields[2], in.fields[3]}.with_override(
                in.button_override.names,
                in.button_override.defaults
            );
        id.joy = svc::input::build_joy_plan(lists, in.gamepad_defaults);
        (void)id.j_names.assign(lists.j);
    }
    id.joy.stick_dpad = !p.host.suppress_stick_dpad;
    (void)id.core.assign(eff);
    (void)id.conf_name.assign(name);
    id.stem = RememberedStem::of(name, facts);
    id.rotation = facts.rotation;
    id.setname_same_dir = facts.setname_same_dir;
    id.host = infra::OptRef<const cores::HostPolicy>{p.host};
    id.slots = p.slots;
    id.game_id = game_id_of(facts, in.log_file_entry);

    out.mgl_playable = !p.host.front_end && !p.suppresses_mgl;
    out.stamp = in.stamp;
    return out;
}

}
