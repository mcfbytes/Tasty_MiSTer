// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <memory>

#include "infra/error.h"
#include "hal/bridge_sequencer.h"
#include "infra/opt_ref.h"
#include "hal/fpga_programmer.h"
#include "hal/board_link.h"
#include "hal/ocram_platform.h"
#include "hal/ocram_run.h"
#include "infra/prelude.h"

namespace mister::hal {
struct BoardProfile;
class BoardWindows;
}

namespace mister::infra::xthread {
struct RtStats;
}

namespace mister::hal {
class DiskLamp;
}

namespace mister::fw {

void release_link(hal::IBoardLink& link);

class BoardParts {
public:
    struct Refusal {
        const char* role;
        Error why;
    };

    [[nodiscard]] static std::expected<BoardParts, Refusal> make(
        const hal::BoardProfile& profile,
        hal::BoardWindows& windows,
        xthread::RtStats& stats
    );

    ~BoardParts();
    BoardParts(BoardParts&&) noexcept;
    BoardParts& operator=(BoardParts&&) noexcept;
    BoardParts(const BoardParts&) = delete;
    BoardParts& operator=(const BoardParts&) = delete;

    hal::IBoardLink& link() const noexcept { return *link_; }

    [[nodiscard]] infra::OptRef<hal::DiskLamp> disk_lamp() const noexcept {
        return infra::opt_ref_of(lamp_.get());
    }

    hal::IFpgaProgrammer& programmer() const noexcept { return *programmer_; }

    hal::IBridgeSequencer& bridges() const noexcept { return *bridges_; }

    [[nodiscard]] infra::OptRef<hal::IOcramPlatform> ocram_platform() const noexcept {
        return infra::opt_ref_of(ocram_.get());
    }

    [[nodiscard]] infra::OptRef<hal::IOcramRun> ocram_run() const noexcept { return ocram_run_; }

private:
    BoardParts(
        std::unique_ptr<hal::IBoardLink> link,
        std::unique_ptr<hal::IFpgaProgrammer> programmer,
        std::unique_ptr<hal::IBridgeSequencer> bridges,
        std::unique_ptr<hal::IOcramPlatform> ocram,
        hal::IOcramRun* run,
        std::unique_ptr<hal::DiskLamp> lamp
    ) noexcept;

    std::unique_ptr<hal::IBoardLink> link_;
    std::unique_ptr<hal::IFpgaProgrammer> programmer_;
    std::unique_ptr<hal::IBridgeSequencer> bridges_;
    std::unique_ptr<hal::IOcramPlatform> ocram_;
    infra::OptRef<hal::IOcramRun> ocram_run_;
    std::unique_ptr<hal::DiskLamp> lamp_;
};

}
