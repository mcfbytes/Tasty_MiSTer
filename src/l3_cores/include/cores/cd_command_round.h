// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>

#include "cores/cd_command_words.h"
#include "cores/cd_dialect_ports.h"
#include "cores/cd_engine.h"
#include "cores/cd_wire_link.h"
#include "cores/cd_wire_port.h"
#include "proto/image_sink.h"
#include "svc/disc_read_service.h"

namespace mister::cores {

inline void run_command_round(
    CdWirePort& wire,
    CdEngine& eng,
    const svc::DiscGeometry& geom,
    proto::IImageSink& sink
) noexcept {
    const std::optional<CdCommandWords> cmd = wire.read_command_edge(eng);
    CdWireLink link{wire, sink};
    CdDialectPorts p{geom, link};
    if (cmd) {
        if (auto r = eng.accept_command(cmd->view(), p); !r)
            wire.count_fault();
    }
    eng.after_command_round();
}

}
