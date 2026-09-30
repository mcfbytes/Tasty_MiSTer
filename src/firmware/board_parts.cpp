// SPDX-License-Identifier: GPL-3.0-or-later
#include "board_parts.h"

#include <utility>

#include "infra/seat.h"
#include "hal/board_profile.h"
#include "hal/board_windows.h"
#include "hal/spi_bus.h"
#include "boards/agilex_5/agilex5_bridges.h"
#include "boards/agilex_5/agilex5_programmer.h"
#include "boards/cyclone_v/cyclone_v_bridges.h"
#include "boards/cyclone_v/cyclone_v_programmer.h"
#include "boards/refusing_link_port.h"
#include "boards/spi_sbc/spi_sbc_bridges.h"
#include "boards/spi_sbc/spi_sbc_programmer.h"
#include "boards/zynq_7000/zynq7000_bridges.h"
#include "boards/zynq_7000/zynq7000_programmer.h"

namespace mister::fw {

void release_link(hal::ILinkPort& link) {
    const mister::SeatScope stands_in_for_rt{mister::SeatTag::RT};
    link.deselect();
    link.set_core_reset(false);
}

namespace {

std::unexpected<BoardParts::Refusal> refuse_after_link(hal::ILinkPort& link, const char* role,
                                                       const Error& why) {
    release_link(link);
    return std::unexpected(BoardParts::Refusal{role, why});
}

}  // namespace

BoardParts::BoardParts(std::unique_ptr<hal::ILinkPort> link,
                       std::unique_ptr<hal::IFpgaProgrammer> programmer,
                       std::unique_ptr<hal::IBridgeSequencer> bridges) noexcept
    : link_(std::move(link)), programmer_(std::move(programmer)), bridges_(std::move(bridges)) {}

std::expected<BoardParts, BoardParts::Refusal> BoardParts::make(const hal::BoardProfile& profile,
                                                                hal::BoardWindows& windows,
                                                                xthread::RtStats& stats) {
    switch (profile.id) {
        case hal::BoardId::De10Nano: {
            auto mailbox = windows.take_mailbox();
            if (!mailbox) return std::unexpected(Refusal{"SpiBus::open", mailbox.error()});
            auto bus = hal::SpiBus::open(std::move(*mailbox), &stats);
            if (!bus) return std::unexpected(Refusal{"SpiBus::open", bus.error()});
            auto link = std::make_unique<hal::SpiBus>(std::move(*bus));
            auto prog = boards::CycloneVProgrammer::open(windows);
            if (!prog)
                return refuse_after_link(*link, "BoardParts::make (CycloneVProgrammer::open)",
                                         prog.error());
            auto brg = boards::CycloneVBridges::open(windows);
            if (!brg)
                return refuse_after_link(*link, "BoardParts::make (CycloneVBridges::open)",
                                         brg.error());
            return BoardParts(std::move(link),
                              std::make_unique<boards::CycloneVProgrammer>(std::move(*prog)),
                              std::make_unique<boards::CycloneVBridges>(std::move(*brg)));
        }
        case hal::BoardId::De25Nano:
            return BoardParts(std::make_unique<boards::RefusingLinkPort>(),
                              std::make_unique<boards::Agilex5Programmer>(),
                              std::make_unique<boards::Agilex5Bridges>());
        case hal::BoardId::Zynq7000:
            return BoardParts(std::make_unique<boards::RefusingLinkPort>(),
                              std::make_unique<boards::Zynq7000Programmer>(),
                              std::make_unique<boards::Zynq7000Bridges>());
        case hal::BoardId::SpiSbc:
            return BoardParts(std::make_unique<boards::RefusingLinkPort>(),
                              std::make_unique<boards::SpiSbcProgrammer>(),
                              std::make_unique<boards::SpiSbcBridges>());
    }

    return std::unexpected(Refusal{"BoardParts::make", Error{Errc::dt_missing, ERR_SITE(), 0}});
}

}  // namespace mister::fw
