// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay_block.h"

#include "infra/wired_end.h"

namespace mister::fw {

using infra::wired_end;

ReplayBlock::ReplayBlock(
    const BootParts& plat,
    const DeviceBlock::Parts& parts,
    app::ScreenshotPump& shots,
    app::IOsdClose& osd
)
    : replay_gate_{app::replay::ReplayGate::Wiring{
          .link = plat.link,
          .frames = parts.wires.frames,
          .emitter = parts.wires.input_emit.emitter(),
          .clock = parts.wires.clock,
          .control = replay_control_,
          .status = replay_status_,
          .vsync = parts.wires.vsync,
          .relay = infra::OptRef<const app::PadRelayTally>{parts.wires.input_emit.relay_tally()}
      }},
      replay_feeder_{app::replay::ReplayFeeder::Wiring{
          .vfs = infra::OptRef<const svc::Vfs>{plat.vfs},
          .clock = parts.wires.clock,
          .ring = wired_end(replay_ring_.sender(), "ReplayRing sender"),
          .control = replay_control_,
          .status = replay_status_,
          .identity = parts.session.link.identity(),
          .core_status = parts.session.link.status_cell(),
          .conf = parts.cells.conf_str,
          .asks = parts.session.owner.ui_requests(),
          .output_lock = parts.wires.video.output_lock(),
          .input = parts.wires.core_input,
          .shots = shots,
          .diag = parts.wires.diag,
          .osd = osd,
          .settings_tx = parts.wires.ui_inbox
      }} {}

}
