// SPDX-License-Identifier: GPL-3.0-or-later
#include "recorder_block.h"

#include "report.h"

namespace mister::fw {

std::optional<hal::ScalerBuffers> RecorderBlock::map_scaler_buffers(
    std::span<const hal::PhysRegion> regions) {
    std::optional<hal::ScalerBuffers> out;
    if (regions.size() != hal::kRegionCount) return out;

    auto m = hal::ScalerBuffers::map(regions[static_cast<std::size_t>(hal::RegionId::ScalerOut)]);
    if (m) {
        out.emplace(std::move(*m));
    } else {
        report("ScalerBuffers::map (degraded: every rec_start answers no_window)", m.error());
    }
    return out;
}

RecorderBlock::RecorderBlock(const BootParts& plat, DeviceBlock& device,
                             const app::ReplayStatusCell& replay_status)
    : copier_{app::FrameCopier::Wiring{.clock = &device.clock_,
                                       .delay = &capture_delay_,
                                       .control = &rec_control_,
                                       .status = &rec_status_,
                                       .demand = &device.frame_demand_,
                                       .frames = &device.core_frames_,
                                       .replay = &replay_status,
                                       .writer = &rec_write_status_,
                                       .avi_writer = &avi_write_status_,
                                       .video = &device.video_pump_.geometry_cell(),
                                       .channel = &raw_frames_},
              map_scaler_buffers(plat.regions)},
      avi_encoder_{app::AviEncoder::Wiring{
          .out = &chunks_, .raw = &raw_frames_, .clock = &device.clock_, .cpu = &encode_cpu_}},
      hasher_{app::FrameHasher::Wiring{.channel = &raw_frames_,
                                       .out = &sidecar_ring_,
                                       .writer_wake = &rec_write_wake_,
                                       .status = &encode_status_,
                                       .clock = &device.clock_,
                                       .video = &avi_encoder_}},
      sidecar_writer_{
          app::SidecarWriter::Wiring{.in = &sidecar_ring_, .status = &rec_write_status_}},
      avi_writer_{app::AviWriter::Wiring{.in = &chunks_, .status = &avi_write_status_}},
      recorder_{app::RecorderControl::Wiring{.control = &rec_control_,
                                             .capture_wake = &capture_wake_,
                                             .status = &rec_status_,
                                             .writer = &rec_write_status_,
                                             .diag = &device.diag_log_,
                                             .identity = &device.session_.identity()}} {}

void RecorderBlock::open_seats(ThreadAssembly::SeatMains& mains) noexcept {
    if (auto c = xthread::ParkFds::create(capture_wake_); !c) {
        report("CaptureMain::open (degraded: no recorder)", c.error());
    } else if (auto e = xthread::ParkFds::create(encode_wake_); !e) {
        report("EncodeMain::open (degraded: no recorder)", e.error());
    } else if (auto w = xthread::ParkFds::create(rec_write_wake_); !w) {
        report("RecWriteMain::open (degraded: no recorder)", w.error());
    } else {
        capture_main_.emplace(copier_, std::move(*c));
        encode_main_.emplace(hasher_, std::move(*e), encode_delay_);
        rec_write_main_.emplace(sidecar_writer_, avi_writer_, std::move(*w));
        mains.mains.bind(&*capture_main_);
        mains.mains.bind(&*encode_main_);
        mains.mains.bind(&*rec_write_main_);
    }
}

}  // namespace mister::fw
