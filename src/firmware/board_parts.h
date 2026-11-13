// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <memory>

#include "infra/error.h"
#include "hal/bridge_sequencer.h"
#include "infra/opt_ref.h"
#include "hal/fpga_programmer.h"
#include "hal/link_port.h"

namespace mister::hal {
struct BoardProfile;
class BoardWindows;
}

namespace mister::xthread {
struct RtStats;
}

namespace mister::hal {
class DiskLamp;
}

namespace mister::fw {

void release_link(hal::ILinkPort& link);

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

    hal::ILinkPort& link() const noexcept { return *link_; }

    [[nodiscard]] infra::OptRef<hal::DiskLamp> disk_lamp() const noexcept {
        return infra::opt_ref_of(lamp_.get());
    }

    hal::IFpgaProgrammer& programmer() const noexcept { return *programmer_; }

    hal::IBridgeSequencer& bridges() const noexcept { return *bridges_; }

private:
    BoardParts(
        std::unique_ptr<hal::ILinkPort> link,
        std::unique_ptr<hal::IFpgaProgrammer> programmer,
        std::unique_ptr<hal::IBridgeSequencer> bridges,
        std::unique_ptr<hal::DiskLamp> lamp
    ) noexcept;

    std::unique_ptr<hal::ILinkPort> link_;
    std::unique_ptr<hal::IFpgaProgrammer> programmer_;
    std::unique_ptr<hal::IBridgeSequencer> bridges_;
    std::unique_ptr<hal::DiskLamp> lamp_;
};

}
