// SPDX-License-Identifier: GPL-3.0-or-later
#include "recorder_block.h"

#include "open_verdict.h"

namespace mister::fw {

std::optional<hal::ScalerBuffers> RecorderBlock::map_scaler_buffers(
    std::span<const hal::PhysRegion> regions,
    OpenVerdict& verdict
) {
    std::optional<hal::ScalerBuffers> out;

    const auto region = hal::declared_region(regions, hal::RegionId::ScalerOut);
    if (!region)
        return out;
    if (auto m = hal::ScalerBuffers::map(*region); m) {
        out.emplace(std::move(*m));
    } else {
        verdict.refuse("T-CAPTURE: ScalerBuffers::map", m.error());
    }
    return out;
}

RecorderBlock::RecorderBlock(
    const BootParts& plat,
    DeviceBlock& device,
    const app::ReplayStatusCell& replay_status
)
    : copier_{
          app::FrameCopier::Wiring{
              .clock = device.clock_,
              .delay = capture_delay_,
              .control = rec_control_,
              .status = rec_status_,
              .demand = device.frame_demand_,
              .frames = device.core_frames_,
              .replay = replay_status,
              .writer = rec_write_status_,
              .avi_writer = avi_write_status_,
              .video = device.video_pump_.geometry_cell(),
              .channel = raw_frames_
          },
          map_scaler_buffers(plat.regions, device.verdict())
      },
      avi_encoder_{app::AviEncoder::Wiring{
          .out = infra::OptRef<app::ChunkChannel>{chunks_},
          .raw = infra::OptRef<const app::RawFrameChannel>{raw_frames_},
          .clock = infra::OptRef<const os::IClock>{device.clock_},
          .cpu = infra::OptRef<const os::IClock>{encode_cpu_}
      }},
      hasher_{app::FrameHasher::Wiring{
          .channel = raw_frames_,
          .out = sidecar_ring_,
          .writer_wake = rec_write_wake_,
          .status = encode_status_,
          .clock = device.clock_,
          .video = avi_encoder_
      }},
      sidecar_writer_{app::SidecarWriter::Wiring{.in = sidecar_ring_, .status = rec_write_status_}},
      avi_writer_{app::AviWriter::Wiring{.in = chunks_, .status = avi_write_status_}},
      recorder_{app::RecorderControl::Wiring{
          .control = infra::OptRef<app::RecControlCell>{rec_control_},
          .capture_wake = infra::OptRef<xthread::WakeFlag>{capture_wake_},
          .status = infra::OptRef<const app::RecorderStatusCell>{rec_status_},
          .writer = infra::OptRef<const app::RecWriteStatusCell>{rec_write_status_},
          .diag = infra::OptRef<xthread::DiagLog>{device.diag_log_},
          .identity = infra::OptRef<const app::IdentityLatch>{device.session_.identity()}
      }} {

    OpenVerdict& verdict = device.verdict();
    if (auto c = xthread::ParkFds::create(capture_wake_); !c) {
        verdict.refuse("T-CAPTURE: ParkFds::create", c.error());
    } else if (auto e = xthread::ParkFds::create(encode_wake_); !e) {
        verdict.refuse("T-ENCODE: ParkFds::create", e.error());
    } else if (auto w = xthread::ParkFds::create(rec_write_wake_); !w) {
        verdict.refuse("T-RECWRITE: ParkFds::create", w.error());
    } else {
        capture_main_.emplace(copier_, std::move(*c));
        encode_main_.emplace(hasher_, std::move(*e), encode_delay_);
        rec_write_main_.emplace(sidecar_writer_, avi_writer_, std::move(*w));
    }
}

void RecorderBlock::open_seats(ThreadAssembly::SeatMains& mains) noexcept {
    mains.mains.bind(&seated(capture_main_, "T-CAPTURE bound past a refused open"));
    mains.mains.bind(&seated(encode_main_, "T-ENCODE bound past a refused open"));
    mains.mains.bind(&seated(rec_write_main_, "T-RECWRITE bound past a refused open"));
}

}
