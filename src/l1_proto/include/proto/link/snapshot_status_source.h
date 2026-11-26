// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>

#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "hal/sys_block.h"
#include "proto/build_generation.h"
#include "proto/link/status_change.h"
#include "proto/link/status_snapshot.h"
#include "proto/link/status_source.h"
#include "proto/status_word.h"

namespace mister::proto::link {

class SnapshotStatusSource final : public IStatusSource {
    TASTY_SEAT_RESIDENT(RT);

public:
    void bind(infra::OptRef<const StatusSnapshotCell> src, BuildGeneration gen) noexcept;

    [[nodiscard]] std::optional<StatusChange> take(
        StatusChange held,
        StatusWord& words
    ) noexcept override;

    [[nodiscard]] std::uint32_t adopted() const noexcept { return adopted_; }

    [[nodiscard]] std::uint32_t stale() const noexcept { return stale_; }

private:
    infra::OptRef<const StatusSnapshotCell> src_{};
    StatusSnapshotCell::Reader reader_{};
    BuildGeneration gen_{};
    std::uint32_t adopted_ = 0;
    std::uint32_t stale_ = 0;
    StatusSnapshot scratch_{};
};

}
