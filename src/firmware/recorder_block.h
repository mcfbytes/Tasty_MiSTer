// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <span>

#include "app/avi_encoder.h"
#include "app/avi_write_status.h"
#include "app/avi_writer.h"
#include "app/capture_main.h"
#include "app/chunk_slot.h"
#include "app/encode_main.h"
#include "app/encode_status.h"
#include "app/frame_copier.h"
#include "app/frame_hasher.h"
#include "app/raw_frame_slot.h"
#include "app/rec_control.h"
#include "app/rec_write_main.h"
#include "app/rec_write_status.h"
#include "app/recorder_control.h"
#include "app/recorder_status.h"
#include "app/replay_status.h"
#include "boot_parts.h"
#include "device_block.h"
#include "hal/scaler_buffers.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
#include "os/nanosleep_delay.h"
#include "os/thread_cpu_clock.h"
#include "thread_assembly.h"

namespace mister::fw {

class RecorderBlock {
    TASTY_SEAT_EXEMPT(boot);

public:
    RecorderBlock(const BootParts& plat, DeviceBlock& device,
                  const app::ReplayStatusCell& replay_status);
    RecorderBlock(const RecorderBlock&) = delete;
    RecorderBlock& operator=(const RecorderBlock&) = delete;

    void open_seats(ThreadAssembly::SeatMains& mains) noexcept;

private:
    [[nodiscard]] static std::optional<hal::ScalerBuffers> map_scaler_buffers(
        std::span<const hal::PhysRegion> regions);

public:
    app::RecControlCell rec_control_{};
    app::RecorderStatusCell rec_status_{};
    app::EncodeStatusCell encode_status_{};
    app::RecWriteStatusCell rec_write_status_{};
    xthread::WakeFlag capture_wake_{};
    xthread::WakeFlag encode_wake_{};
    xthread::WakeFlag rec_write_wake_{};
    app::RawFrameChannel raw_frames_{encode_wake_, xthread::Polled{}};
    app::SidecarRing sidecar_ring_{};
    app::AviWriteStatusCell avi_write_status_{};
    app::ChunkChannel chunks_{rec_write_wake_, xthread::Polled{}};
    os::ThreadCpuClock encode_cpu_{};
    os::NanosleepDelay encode_delay_{};
    os::NanosleepDelay capture_delay_{};
    app::FrameCopier copier_;
    app::AviEncoder avi_encoder_;
    app::FrameHasher hasher_;
    app::SidecarWriter sidecar_writer_;
    app::AviWriter avi_writer_;
    std::optional<app::CaptureMain> capture_main_;
    std::optional<app::EncodeMain> encode_main_;
    std::optional<app::RecWriteMain> rec_write_main_;
    app::RecorderControl recorder_;
};

}  // namespace mister::fw
