// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay_block.h"

namespace mister::fw {

ReplayBlock::ReplayBlock(const BootParts& plat, DeviceBlock& device, app::ScreenshotPump& shots,
                         app::IOsdClose& osd)
    : replay_gate_{app::ReplayGate::Wiring{
          .link = &plat.link,
          .frames = &device.frame_counter_,
          .emitter = &device.input_emit_.emitter(),
          .clock = &device.clock_,
          .control = &replay_control_,
          .status = &replay_status_,
          .vsync = device.frame_ != nullptr ? &device.frame_->frame_cell() : nullptr}},
      replay_feeder_{app::ReplayFeeder::Wiring{.vfs = &plat.vfs,
                                               .clock = &device.clock_,
                                               .ring = &replay_ring_,
                                               .control = &replay_control_,
                                               .status = &replay_status_,
                                               .identity = &device.session_.identity(),
                                               .core_status = &device.session_.status_cell(),
                                               .conf = &device.conf_str_cell_,
                                               .asks = &device.owner_.ui_requests(),
                                               .video = &device.video_pump_,
                                               .input = &device.input_wire_,
                                               .shots = &shots,
                                               .diag = &device.diag_log_,
                                               .osd = &osd,
                                               .settings_tx = &device.ui_inbox_}} {}

}  // namespace mister::fw
