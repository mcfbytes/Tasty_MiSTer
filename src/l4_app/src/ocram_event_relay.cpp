// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ocram_event_relay.h"

#include <array>
#include <cstddef>

#include "hal/link_halves.h"
#include "hal/ocram_record.h"
#include "proto/status_word.h"
#include "svc/types.h"

namespace mister::app {

static_assert(hal::ocram::RumbleArg::kPads == svc::kMaxPlayers, "the RUMBLE pad is the player");

void OcramEventRelay::bind(proto::BuildGeneration gen, proto::BindGeneration conf_gen) noexcept {
    TASTY_SEAT_BODY(OcramEventRelay);
    gen_ = gen;
    info_.bind_gen = conf_gen;
}

void OcramEventRelay::unbind() noexcept {
    TASTY_SEAT_BODY(OcramEventRelay);
    if (!gen_.built())
        return;
    gen_ = proto::BuildGeneration{};
    rumble_.levels = {};
    publish_rumble_();
}

void OcramEventRelay::status(std::span<const std::uint32_t> words) noexcept {
    TASTY_SEAT_BODY(OcramEventRelay);
    if (!gen_.built())
        return;
    if (!hal::ocram::RecordHeader::kStatusWords.matches(words)) {
        tally_.malformed.add();
        return;
    }
    std::array<hal::LinkHalves, proto::StatusWord::kRegisters> regs{};
    for (std::size_t r = 0; r < regs.size(); ++r)
        regs[r] = hal::LinkHalves::of(words[r]);
    stflg_ = stflg_.next();
    w_.status.publish(
        proto::link::StatusSnapshot{
            .stflg = stflg_,
            .words = proto::StatusWord::of_registers(regs),
            .gen = gen_
        }
    );
    tally_.status.add();
}

void OcramEventRelay::info(hal::ocram::InfoArg arg) noexcept {
    TASTY_SEAT_BODY(OcramEventRelay);
    if (!gen_.built())
        return;
    info_.info = arg;
    info_.count.add();
    w_.info.publish(info_);
    tally_.info.add();
}

void OcramEventRelay::upload(hal::ocram::UploadArg) noexcept {
    TASTY_SEAT_BODY(OcramEventRelay);
    if (gen_.built())
        tally_.uploads.add();
}

void OcramEventRelay::rumble(
    hal::ocram::RumbleArg arg,
    std::span<const std::uint32_t> words
) noexcept {
    TASTY_SEAT_BODY(OcramEventRelay);
    if (!gen_.built())
        return;
    if (!hal::ocram::RecordHeader::kRumbleWords.matches(words)) {
        tally_.malformed.add();
        return;
    }
    rumble_.levels[player_of_(arg)] = hal::LinkHalves::of(words[0]).low;
    publish_rumble_();
    tally_.rumble.add();
}

static_assert(hal::ocram::RumbleArg::kPads == svc::kMaxPlayers, "the RUMBLE pad is the player");

svc::PlayerIndex OcramEventRelay::player_of_(hal::ocram::RumbleArg pad) noexcept {
    return svc::kPlayers[pad.pad()];
}

void OcramEventRelay::publish_rumble_() noexcept {
    rumble_.count.add();
    w_.rumble.publish(rumble_);
    w_.input_wake.kick();
}

}
