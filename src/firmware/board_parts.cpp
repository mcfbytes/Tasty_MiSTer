// SPDX-License-Identifier: GPL-3.0-or-later
#include "board_parts.h"

#include "hal/disk_lamp.h"

#include <cstdio>
#include <utility>

#include "infra/seat.h"
#include "hal/board_profile.h"
#include "hal/board_windows.h"
#include "hal/spi_bus.h"
#include "boards/cyclone_v/cyclone_v_bridges.h"
#include "boards/cyclone_v/cyclone_v_programmer.h"
#include "boards/refusing_bridges.h"
#include "boards/refusing_link_port.h"
#include "infra/prelude.h"
#include "boards/refusing_programmer.h"

namespace mister::fw {

void release_link(hal::ILinkPort& link) {
    const mister::SeatScope stands_in_for_rt{mister::SeatTag::RT};
    link.deselect();
    link.set_core_reset(false);
}

namespace {

std::unexpected<BoardParts::Refusal> refuse_after_link(
    hal::ILinkPort& link,
    const char* role,
    const Error& why
) {
    release_link(link);
    return std::unexpected(BoardParts::Refusal{role, why});
}

}

BoardParts::~BoardParts() = default;
BoardParts::BoardParts(BoardParts&&) noexcept = default;
BoardParts& BoardParts::operator=(BoardParts&&) noexcept = default;

BoardParts::BoardParts(
    std::unique_ptr<hal::ILinkPort> link,
    std::unique_ptr<hal::IFpgaProgrammer> programmer,
    std::unique_ptr<hal::IBridgeSequencer> bridges,
    std::unique_ptr<hal::DiskLamp> lamp
) noexcept
    : link_(std::move(link)),
      programmer_(std::move(programmer)),
      bridges_(std::move(bridges)),
      lamp_(std::move(lamp)) {}

std::expected<BoardParts, BoardParts::Refusal> BoardParts::make(
    const hal::BoardProfile& profile,
    hal::BoardWindows& windows,
    xthread::RtStats& stats
) {
    switch (profile.id) {
        case hal::BoardId::De10Nano: {
            auto mailbox = windows.take_mailbox();
            if (!mailbox)
                return std::unexpected(Refusal{"SpiBus::open", mailbox.error()});

            auto bus = hal::SpiBus::open(std::move(*mailbox), hal::LookPolicy::TakeOver, &stats);
            if (!bus)
                return std::unexpected(Refusal{"SpiBus::open", bus.error()});
            if (bus->look == hal::LinkLook::LwHostEnabled) {
                std::fprintf(
                    stderr,
                    "mister: the LW host enable was held at startup: taking it over, "
                    "the identification clears it\n"
                );
            }
            auto link = std::make_unique<hal::SpiBus>(std::move(bus->bus));
            auto prog = boards::cyclone_v::Programmer::open(windows);
            if (!prog)
                return refuse_after_link(
                    *link,
                    "BoardParts::make (cyclone_v::Programmer::open)",
                    prog.error()
                );
            auto brg = boards::cyclone_v::Bridges::open(windows);
            if (!brg)
                return refuse_after_link(
                    *link,
                    "BoardParts::make (cyclone_v::Bridges::open)",
                    brg.error()
                );

            auto lamp = std::make_unique<hal::DiskLamp>(*link);
            return BoardParts(
                std::move(link),
                std::make_unique<boards::cyclone_v::Programmer>(std::move(*prog)),
                std::make_unique<boards::cyclone_v::Bridges>(std::move(*brg)),
                std::move(lamp)
            );
        }
        case hal::BoardId::De25Nano:
            return BoardParts(
                std::make_unique<boards::RefusingLinkPort>(),
                std::make_unique<boards::RefusingProgrammer>(),
                std::make_unique<boards::RefusingBridges>(),
                nullptr
            );
        case hal::BoardId::Zynq7000:
            return BoardParts(
                std::make_unique<boards::RefusingLinkPort>(),
                std::make_unique<boards::RefusingProgrammer>(),
                std::make_unique<boards::RefusingBridges>(),
                nullptr
            );
        case hal::BoardId::SpiSbc:
            return BoardParts(
                std::make_unique<boards::RefusingLinkPort>(),
                std::make_unique<boards::RefusingProgrammer>(),
                std::make_unique<boards::RefusingBridges>(),
                nullptr
            );
    }

    return std::unexpected(Refusal{"BoardParts::make", Error{Errc::dt_missing, ERR_SITE(), 0}});
}

}
