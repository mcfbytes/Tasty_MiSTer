// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <span>

#include "app/load_window.h"
#include "app/load_window_map.h"
#include "cores/core_window_decl.h"
#include "cores/disk_mirror.h"
#include "hal/fpga_aperture.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "infra/seat_owner_main.h"
#include "infra/tally.h"
#include "proto/types.h"
#include "svc/mirror_source.h"

namespace mister::app {

class MirrorFill {
    TASTY_SEAT_RESIDENT(Main);

public:
    struct Tally {
        infra::Tally pieces{};
        proto::FileSize bytes{};
    };

    [[nodiscard]] static Ex<MirrorFill> open(
        std::unique_ptr<svc::IMirrorSource> source,
        const cores::DiskMirror& mirror,
        std::span<const cores::CoreWindowDecl> windows,
        const hal::FpgaAperture& aperture,
        ILoadWindowMap<seat_owner::Main>& map
    );

    [[nodiscard]] Ex<bool> step();

    [[nodiscard]] bool finished() const noexcept { return handed_; }

    [[nodiscard]] const Tally& tally() const noexcept { return tally_; }

private:
    MirrorFill(
        std::unique_ptr<svc::IMirrorSource> source,
        LoadWindow<seat_owner::Main> window
    ) noexcept;

    std::unique_ptr<svc::IMirrorSource> source_;
    LoadWindow<seat_owner::Main> window_;
    bool handed_ = false;
    Tally tally_{};
};

}
