// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "infra/tally.h"

namespace mister::svc {

struct NicPoll {
    infra::Tally commands{};
    infra::Tally overruns{};
    infra::Tally frames_in{};
    infra::Tally frames_out{};

    [[nodiscard]] constexpr NicPoll after(const NicPoll& total) const noexcept {
        return NicPoll{
            .commands = total.commands.sum(commands),
            .overruns = total.overruns.sum(overruns),
            .frames_in = total.frames_in.sum(frames_in),
            .frames_out = total.frames_out.sum(frames_out)
        };
    }

    friend constexpr bool operator==(const NicPoll&, const NicPoll&) = default;
};

}
