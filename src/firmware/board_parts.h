// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <memory>

#include "infra/error.h"
#include "hal/bridge_sequencer.h"
#include "hal/fpga_programmer.h"
#include "hal/link_port.h"

namespace mister::hal {
struct BoardProfile;
class BoardWindows;
}  // namespace mister::hal

namespace mister::xthread {
struct RtStats;
}

namespace mister::fw {

void release_link(hal::ILinkPort& link);

class BoardParts {
public:
    struct Refusal {
        const char* role;
        Error why;
    };

    [[nodiscard]] static std::expected<BoardParts, Refusal> make(const hal::BoardProfile& profile,
                                                                 hal::BoardWindows& windows,
                                                                 xthread::RtStats& stats);

    BoardParts(BoardParts&&) noexcept = default;
    BoardParts& operator=(BoardParts&&) noexcept = default;
    BoardParts(const BoardParts&) = delete;
    BoardParts& operator=(const BoardParts&) = delete;

    hal::ILinkPort& link() const noexcept { return *link_; }
    hal::IFpgaProgrammer& programmer() const noexcept { return *programmer_; }
    hal::IBridgeSequencer& bridges() const noexcept { return *bridges_; }

private:
    BoardParts(std::unique_ptr<hal::ILinkPort> link,
               std::unique_ptr<hal::IFpgaProgrammer> programmer,
               std::unique_ptr<hal::IBridgeSequencer> bridges) noexcept;

    std::unique_ptr<hal::ILinkPort> link_;
    std::unique_ptr<hal::IFpgaProgrammer> programmer_;
    std::unique_ptr<hal::IBridgeSequencer> bridges_;
};

}  // namespace mister::fw
