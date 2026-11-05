// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay_block.h"

namespace mister::fw {

ReplayBlock::ReplayBlock(
    const BootParts& plat,
    const DeviceBlock::Parts& parts,
    app::ScreenshotPump& shots,
    app::IOsdClose& osd
)
    : replay_gate_{app::ReplayGate::Wiring{
          .link = plat.link,
          .frames = parts.wires.frames,
          .emitter = parts.wires.input_emit.emitter(),
          .clock = parts.wires.clock,
          .control = replay_control_,
          .status = replay_status_,
          .vsync = parts.wires.vsync
      }},
      replay_feeder_{app::ReplayFeeder::Wiring{
          .vfs = infra::OptRef<const svc::Vfs>{plat.vfs},
          .clock = parts.wires.clock,
          .ring = wired_end(replay_ring_.sender(), "ReplayRing sender"),
          .control = replay_control_,
          .status = replay_status_,
          .identity = parts.session.link.identity(),
          .core_status = parts.session.link.status_cell(),
          .conf = parts.cells.conf_str,
          .asks = parts.session.owner.ui_requests(),
          .video = parts.wires.video,
          .input = parts.wires.input,
          .shots = shots,
          .diag = parts.wires.diag,
          .osd = osd,
          .settings_tx = parts.wires.ui_inbox
      }} {}

}
