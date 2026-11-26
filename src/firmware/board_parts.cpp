// SPDX-License-Identifier: GPL-3.0-or-later
#include "board_parts.h"

#include "hal/disk_lamp.h"

#include <cstdio>
#include <utility>

#include "infra/seat.h"
#include "hal/board_profile.h"
#include "hal/board_windows.h"
#include "hal/gpo_gpi_port.h"
#include "boards/cyclone_v/cyclone_v_bridges.h"
#include "boards/cyclone_v/cyclone_v_ocram_platform.h"
#include "boards/cyclone_v/cyclone_v_programmer.h"
#include "boards/refusing_bridges.h"
#include "boards/refusing_board_link.h"
#include "infra/prelude.h"
#include "boards/refusing_programmer.h"

namespace mister::fw {

void release_link(hal::IBoardLink& link) {
    const mister::SeatScope stands_in_for_rt{mister::SeatTag::RT};
    link.deselect();
    link.set_core_reset(false);
}

namespace {

std::unexpected<BoardParts::Refusal> refuse_after_link(
    hal::IBoardLink& link,
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
    std::unique_ptr<hal::IBoardLink> link,
    std::unique_ptr<hal::IFpgaProgrammer> programmer,
    std::unique_ptr<hal::IBridgeSequencer> bridges,
    std::unique_ptr<hal::IOcramPlatform> ocram,
    hal::IOcramRun* run,
    std::unique_ptr<hal::DiskLamp> lamp
) noexcept
    : link_(std::move(link)),
      programmer_(std::move(programmer)),
      bridges_(std::move(bridges)),
      ocram_(std::move(ocram)),
      ocram_run_(infra::opt_ref_of(run)),
      lamp_(std::move(lamp)) {}

std::expected<BoardParts, BoardParts::Refusal> BoardParts::make(
    const hal::BoardProfile& profile,
    hal::BoardWindows& windows,
    xthread::RtStats& stats
) {
    switch (profile.id) {
        case hal::BoardId::De10Nano: {
            auto mailbox = windows.take_gpo_gpi();
            if (!mailbox)
                return std::unexpected(Refusal{"GpoGpiPort::open", mailbox.error()});

            auto bus = hal::GpoGpiPort::open(
                std::move(*mailbox),
                hal::LookPolicy::TakeOver,
                &stats
            );
            if (!bus)
                return std::unexpected(Refusal{"GpoGpiPort::open", bus.error()});
            if (bus->look == hal::LinkLook::LwHostEnabled) {
                std::fprintf(
                    stderr,
                    "mister: the LW host enable was held at startup: taking it over, "
                    "the identification clears it\n"
                );
            }
            auto link = std::make_unique<hal::GpoGpiPort>(std::move(bus->bus));
            hal::IOcramRun* const run = link.get();
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

            auto ocram = boards::cyclone_v::OcramPlatform::open(
                boards::cyclone_v::BridgeLends{brg->rstmgr_lend(), brg->sysmgr_lend()}
            );
            if (!ocram)
                return refuse_after_link(
                    *link,
                    "BoardParts::make (cyclone_v::OcramPlatform::open)",
                    ocram.error()
                );

            auto lamp = std::make_unique<hal::DiskLamp>(*link);
            return BoardParts(
                std::move(link),
                std::make_unique<boards::cyclone_v::Programmer>(std::move(*prog)),
                std::make_unique<boards::cyclone_v::Bridges>(std::move(*brg)),
                std::make_unique<boards::cyclone_v::OcramPlatform>(std::move(*ocram)),
                run,
                std::move(lamp)
            );
        }
        case hal::BoardId::De25Nano:
            return BoardParts(
                std::make_unique<boards::RefusingBoardLink>(),
                std::make_unique<boards::RefusingProgrammer>(),
                std::make_unique<boards::RefusingBridges>(),
                nullptr,
                nullptr,
                nullptr
            );
        case hal::BoardId::Zynq7000:
            return BoardParts(
                std::make_unique<boards::RefusingBoardLink>(),
                std::make_unique<boards::RefusingProgrammer>(),
                std::make_unique<boards::RefusingBridges>(),
                nullptr,
                nullptr,
                nullptr
            );
        case hal::BoardId::SpiSbc:
            return BoardParts(
                std::make_unique<boards::RefusingBoardLink>(),
                std::make_unique<boards::RefusingProgrammer>(),
                std::make_unique<boards::RefusingBridges>(),
                nullptr,
                nullptr,
                nullptr
            );
    }

    return std::unexpected(Refusal{"BoardParts::make", Error{Errc::dt_missing, ERR_SITE(), 0}});
}

}
