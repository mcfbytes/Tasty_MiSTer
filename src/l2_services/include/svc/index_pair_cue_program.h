// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "svc/cue_mount.h"
#include "svc/cue_program.h"

namespace mister::svc {

class IndexPairCueProgram final : public ICueProgram {
    TASTY_SEAT_RESIDENT(Io);

private:
    [[nodiscard]] Ex<void> parse(std::string_view cue_path, const CueMount& m) const override;
    void shift_chd_toc_(Toc& toc, TrackCount count) const override;
    [[nodiscard]] bool pregap_in_own_file_() const override;
};

}
