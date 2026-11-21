// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <span>

#include "app/capture/avi_encoder.h"
#include "app/capture/avi_write_status.h"
#include "app/capture/avi_writer.h"
#include "app/capture/capture_main.h"
#include "app/capture/chunk_slot.h"
#include "app/capture/encode_main.h"
#include "app/capture/encode_status.h"
#include "app/capture/frame_copier.h"
#include "app/capture/frame_hasher.h"
#include "app/capture/raw_frame_slot.h"
#include "app/capture/rec_control.h"
#include "app/capture/rec_write_main.h"
#include "app/capture/rec_write_status.h"
#include "app/recorder_control.h"
#include "app/capture/recorder_status.h"
#include "app/replay_status.h"
#include "boot_parts.h"
#include "device_block.h"
#include "hal/scaler_buffers.h"
#include "infra/xthread/polled.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/xthread/wake_flag.h"
#include "os/nanosleep_delay.h"
#include "os/thread_cpu_clock.h"
#include "thread_assembly.h"

namespace mister::fw {

class RecorderBlock {
    TASTY_SEAT_EXEMPT(boot);

public:
    RecorderBlock(
        const BootParts& plat,
        const DeviceBlock::Parts& parts,
        OpenVerdict& verdict,
        const app::ReplayStatusCell& replay_status
    );
    RecorderBlock(const RecorderBlock&) = delete;
    RecorderBlock& operator=(const RecorderBlock&) = delete;

    void open_seats(ThreadAssembly::SeatMains& mains) noexcept;

private:
    [[nodiscard]] static std::optional<hal::ScalerBuffers> map_scaler_buffers(
        const BootParts& plat,
        OpenVerdict& verdict
    );

public:
    xthread::WakeFlag capture_wake_{};
    app::capture::RecControlChannel rec_control_{capture_wake_, xthread::Polled{}};
    app::capture::RecorderStatusCell rec_status_{};
    app::capture::EncodeStatusCell encode_status_{};
    app::capture::RecWriteStatusCell rec_write_status_{};
    xthread::WakeFlag encode_wake_{};
    xthread::WakeFlag rec_write_wake_{};
    app::capture::RawFrameChannel raw_frames_{encode_wake_, xthread::Polled{}};

    app::capture::SidecarRing sidecar_ring_{rec_write_wake_};
    app::capture::AviWriteStatusCell avi_write_status_{};
    app::capture::ChunkChannel chunks_{rec_write_wake_, xthread::Polled{}};

    os::ThreadCpuClock encode_cpu_{};
    os::NanosleepDelay encode_delay_{};
    os::NanosleepDelay capture_delay_{};
    app::capture::FrameCopier copier_;
    app::capture::AviEncoder avi_encoder_;
    app::capture::FrameHasher hasher_;
    app::capture::SidecarWriter sidecar_writer_;
    app::capture::AviWriter avi_writer_;
    std::optional<app::capture::CaptureMain> capture_main_;
    std::optional<app::capture::EncodeMain> encode_main_;
    std::optional<app::capture::RecWriteMain> rec_write_main_;
    app::RecorderControl recorder_;
};

}
