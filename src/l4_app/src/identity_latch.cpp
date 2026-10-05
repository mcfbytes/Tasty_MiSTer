// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/identity_latch.h"

namespace mister::app {
namespace {

void copy_cstr(char (&dst)[SessionIdentity::kMax], std::string_view src) noexcept {
    const std::size_t n =
        src.size() < SessionIdentity::kMax ? src.size() : SessionIdentity::kMax - 1;
    for (std::size_t i = 0; i < n; ++i)
        dst[i] = src[i];
    for (std::size_t i = n; i < SessionIdentity::kMax; ++i)
        dst[i] = '\0';
}

}  // namespace

void IdentityLatch::publish(std::string_view core, std::string_view rbf, const RememberedStem& stem,
                            const svc::JoyPlan& joy, std::string_view j_names, bool front_end,
                            bool suppress_analog_followup,
                            const svc::IAnalogReshape* analog_reshape, const char* cue_dir,
                            const cores::CheatLookup* cheats,
                            std::span<const cores::FileSlot> slots, const GameId& game_id,
                            bool setname_same_dir, bool image_no_zip) noexcept {

    copy_cstr(scratch_.core, core);
    copy_cstr(scratch_.rbf, rbf);
    scratch_.stem = stem;
    scratch_.setname_same_dir = setname_same_dir;
    scratch_.joy = joy;
    (void)scratch_.j_names.assign(j_names);
    scratch_.front_end = front_end;
    scratch_.suppress_analog_followup = suppress_analog_followup;
    scratch_.analog_reshape = analog_reshape;
    scratch_.cue_dir = cue_dir;
    scratch_.cheats = cheats;
    scratch_.slots = slots;
    scratch_.image_no_zip = image_no_zip;
    scratch_.game_id = game_id;
    cell_.publish(scratch_);
}

bool IdentityLatch::copy(SessionIdentity& out) const noexcept {

    SessionIdentity scratch{};
    if (cell_.sample_into(scratch) == 0) return false;
    out = scratch;
    return true;
}

}  // namespace mister::app
