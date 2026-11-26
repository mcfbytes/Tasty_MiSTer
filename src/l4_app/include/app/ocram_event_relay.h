// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>

#include "app/info_notice.h"
#include "app/rumble_levels.h"
#include "hal/ocram_info_arg.h"
#include "hal/ocram_rumble_arg.h"
#include "hal/ocram_upload_arg.h"
#include "hal/sys_block.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/tally.h"
#include "infra/xthread/wake_flag.h"
#include "proto/build_generation.h"
#include "proto/link/status_snapshot.h"
#include "proto/types.h"
#include "svc/types.h"

namespace mister::app {

class OcramEventRelay {
    TASTY_SEAT_RESIDENT(Io);

public:
    struct Wiring {
        proto::link::StatusSnapshotCell& status;
        InfoNoticeCell& info;
        RumbleLevelsCell& rumble;
        xthread::WakeFlag::Kicker input_wake;
    };

    struct Tallies {
        infra::Tally status{};
        infra::Tally info{};
        infra::Tally uploads{};
        infra::Tally rumble{};
        infra::Tally malformed{};
    };

    explicit OcramEventRelay(const Wiring& w) noexcept : w_(w) {}

    OcramEventRelay(const OcramEventRelay&) = delete;
    OcramEventRelay& operator=(const OcramEventRelay&) = delete;

    void bind(proto::BuildGeneration gen, proto::BindGeneration conf_gen) noexcept;

    void unbind() noexcept;

    [[nodiscard]] bool bound() const noexcept { return gen_.built(); }

    void status(std::span<const std::uint32_t> words) noexcept;
    void info(hal::ocram::InfoArg arg) noexcept;
    void upload(hal::ocram::UploadArg arg) noexcept;
    void rumble(hal::ocram::RumbleArg arg, std::span<const std::uint32_t> words) noexcept;

    [[nodiscard]] const Tallies& tallies() const noexcept { return tally_; }

private:
    void publish_rumble_() noexcept;
    [[nodiscard]] static svc::PlayerIndex player_of_(hal::ocram::RumbleArg pad) noexcept;

    Wiring w_;
    proto::BuildGeneration gen_{};
    hal::SysBlock::Stflg stflg_{};
    InfoNotice info_{};
    RumbleLevels rumble_{};
    Tallies tally_{};
};

}
