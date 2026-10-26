// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

#include "app/companion_host.h"
#include "app/companion_walk.h"
#include "app/negotiated_core.h"
#include "app/owner_tally.h"
#include "cores/companion_load.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"

namespace mister::svc {
class Vfs;
}

namespace mister::app {

class CompanionSlot {
    TASTY_SEAT_RESIDENT(Main);

public:
    CompanionSlot(
        const svc::Vfs& vfs,
        const NegotiatedCore& negotiated,
        OwnerTally& tally,
        std::uint64_t aperture_base,
        infra::OptRef<CompanionHost::Binds> binds
    ) noexcept
        : vfs_(vfs),
          negotiated_(negotiated),
          tally_(tally),
          aperture_base_(aperture_base),
          binds_(binds) {}

    CompanionSlot(const CompanionSlot&) = delete;
    CompanionSlot& operator=(const CompanionSlot&) = delete;

    void make();
    void withdraw() noexcept;

    [[nodiscard]] bool present() const noexcept { return companion_ != nullptr; }

    [[nodiscard]] Ex<CompanionWalk> plan(std::string_view path);

private:
    const svc::Vfs& vfs_;
    const NegotiatedCore& negotiated_;
    OwnerTally& tally_;
    std::uint64_t aperture_base_;
    infra::OptRef<CompanionHost::Binds> binds_;
    std::unique_ptr<cores::ICompanionLoad> companion_{};
    bool attached_ = false;
    std::uint16_t gen_ = 0;
};

}
