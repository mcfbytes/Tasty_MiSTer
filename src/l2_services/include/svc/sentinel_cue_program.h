// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "svc/cue_mount.h"
#include "svc/cue_program.h"
#include "svc/cue_policy.h"

namespace mister::svc {

class SentinelCueProgram final : public ICueProgram {
    TASTY_SEAT_RESIDENT(Io);

public:
    explicit SentinelCueProgram(const CuePolicy& policy) noexcept : policy_(policy) {}

private:
    [[nodiscard]] Ex<void> parse(std::string_view cue_path, const CueMount& m) const override;

    CuePolicy policy_;
};

}
