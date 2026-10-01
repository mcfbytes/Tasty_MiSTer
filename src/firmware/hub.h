// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "app/bt_pump.h"
#include "app/cmd_fifo.h"
#include "app/cmd_router.h"
#include "app/conf_str_cell.h"
#include "app/event.h"
#include "app/input_main.h"
#include "app/input_pipeline.h"
#include "app/ini_parse.h"
#include "app/link_router.h"
#include "app/link_rx_channel.h"
#include "app/link_tx_channel.h"
#include "app/osd_wire.h"
#include "app/avi_encoder.h"
#include "app/avi_write_status.h"
#include "app/avi_writer.h"
#include "app/capture_main.h"
#include "app/chunk_slot.h"
#include "app/core_frame_counter.h"
#include "app/core_frame_record.h"
#include "app/encode_main.h"
#include "app/encode_status.h"
#include "app/frame_copier.h"
#include "app/frame_demand.h"
#include "app/frame_hasher.h"
#include "app/raw_frame_slot.h"
#include "app/rec_control.h"
#include "app/rec_write_main.h"
#include "app/rec_write_status.h"
#include "app/recorder_control.h"
#include "app/recorder_status.h"
#include "app/replay_control.h"
#include "app/replay_feeder.h"
#include "app/replay_gate.h"
#include "app/replay_msg.h"
#include "app/replay_status.h"
#include "app/rt_main.h"
#include "app/scaler_frame_source.h"
#include "app/screenshot_pump.h"
#include "app/screenshot_queue.h"
#include "app/session_owner.h"
#include "app/session_seats.h"
#include "app/link_session.h"
#include "app/owner_tick.h"
#include "app/session_machine.h"
#include "app/ui_request.h"
#include "app/video_pump.h"
#include "assembly.h"
#include "infra/error.h"
#include "infra/rt_stats.h"
#include "os/clock.h"
#include "hal/doorbell_policy.h"
#include "hal/kernel_contract.h"
#include "hal/link_timing.h"
#include "hal/program_geometry.h"
#include "hal/video_out_decl.h"
#include "os/types.h"
#include "proto/link_event.h"
#include "reactor/core_state.h"
#include "reactor/executive.h"
#include "hal/bridge_sequencer.h"
#include "app/bitstream_programmer.h"
#include "hal/fpga_programmer.h"
#include "hal/fpga_memory.h"
#include "hal/scaler_buffers.h"
#include "os/nanosleep_delay.h"
#include "os/thread_cpu_clock.h"
#include "app/dev_mem_window_map.h"
#include "reactor/frame_clock.h"
#include "reactor/frame_main.h"
#include "hal/fpga_aperture.h"
#include "hal/phys_region.h"
#include "reactor/round_timer.h"
#include "hal/link_port.h"
#include "proto/osd_surface.h"
#include "app/pcm_main.h"
#include "app/pcm_ring_feeder.h"
#include "svc/chd_prefetch.h"
#include "svc/prefetch_main.h"
#include "app/durable_write_service.h"
#include "app/window_job_service.h"
#include "svc/disc_read_service.h"
#include "svc/disc_store.h"
#include "svc/io_main.h"
#include "svc/storage_service.h"
#include "svc/vfs.h"

#include "hal/hdmi_int_level.h"
#include "os/kernel_rt.h"
#include "tasty_osd.h"
#include "tasty_sink.h"
#include "ladder_scope.h"
#include "process_main.h"
#include "thread_assembly.h"
#include "infra/seat.h"

namespace mister::fw {

void report(const char* what, const Error& e);

class Hub {
    TASTY_SEAT_EXEMPT(boot);

public:
    struct BootParts {
        hal::ILinkPort& link;
        hal::IBridgeSequencer& bridges;
        hal::IFpgaProgrammer& programmer;
        hal::FpgaAperture fpga_mem;
        hal::PhysRegion lw_window;

        std::span<const hal::PhysRegion> regions;
        hal::DoorbellPolicy doorbells;
        os::UioLineSpace doorbell_nodes;
        hal::VideoOutDecl video;
        hal::KernelContract kernel;
        const hal::ThreadMap& threads;
        reactor::Executive& exec;
        xthread::RtStats& stats;
        const svc::Vfs& vfs;

        RtEvidence& rt_evidence;

        os::KernelRt kernel_rt = os::KernelRt::Unknown;

        hal::LinkTimingValues link_timing;
        hal::ProgramGeometryValues program;
    };

    static constexpr const char* kDiagPath = "/tmp/MiSTer_diag";

    Hub(const BootParts& plat, app::IStopSignal& stop);

    ~Hub();
    Hub(const Hub&) = delete;
    Hub& operator=(const Hub&) = delete;

    app::LinkSession& link_session() noexcept { return session_; }

    app::SessionOwner& owner() noexcept { return owner_; }

    void latch_boot_config() noexcept;
    ThreadAssembly& threads() noexcept { return assembly_; }

    app::RtMain& rt_main() noexcept { return rt_main_; }

    ProcessMain& process_main() noexcept { return process_main_; }

    const ThreadAssembly::SeatMains& seat_mains() const noexcept { return seat_mains_; }

    app::VideoPump& video_pump() noexcept { return video_pump_; }
    app::ReplayFeeder& replay_feeder() noexcept { return replay_feeder_; }
    app::RecorderControl& recorder() noexcept { return recorder_; }
    const app::ReplayStatusCell& replay_status() const noexcept { return replay_status_; }
    const app::RecorderStatusCell& recorder_status() const noexcept { return rec_status_; }
    const app::EncodeStatusCell& encode_status() const noexcept { return encode_status_; }
    const svc::Vfs* vfs() const noexcept { return vfs_; }
    void set_owner_tick(app::IOwnerTick* tick) noexcept { assembly_.ui().set_owner_tick(tick); }
    void force_vsync_adjust(std::uint8_t v) noexcept { video_pump_.force_vsync_adjust(v); }

    void arm_replay_ini(bool strict) noexcept {
        video_pump_.arm_replay_ini(strict);
        owner_.arm_replay_ini(strict);
    }
    const app::IdentityLatch& identity_latch() const noexcept { return session_.identity(); }
    app::UiRequestRing& ui_requests() noexcept { return owner_.ui_requests(); }

    app::LinkTxChannel& ui_link_tx() noexcept { return ui_inbox_; }
    [[nodiscard]] Ex<std::optional<app::UiRequest::LoadCore>> boot_link(
        std::span<const char* const> argv) {
        return session_.boot(argv);
    }
    [[noreturn]] void handoff_executable(const char* exe, std::span<const char* const> argv) {
        session_.handoff_to_alternate_executable(exe, argv);
    }
    void seal_boot_pump() noexcept { session_.seal_boot_pump(); }
    [[nodiscard]] Ex<void> shutdown_link() { return session_.shutdown(); }
    void settle_writes() noexcept { session_.settle_writes(); }
    [[nodiscard]] app::SessionState session_state() const noexcept { return session_.state(); }
    [[nodiscard]] Ex<void> pump_boot() { return rt_main_.pump_boot(); }

private:
    static std::optional<app::CmdFifo> open_cmd_fifo();
    static std::unique_ptr<reactor::FrameClock> open_frame_clock(const char* vsync_device,
                                                                 reactor::Executive& exec);

    [[nodiscard]] static Ex<hal::FpgaMemory> map_scaler_out(
        std::span<const hal::PhysRegion> regions);

    [[nodiscard]] static std::optional<hal::ScalerBuffers> map_scaler_buffers(
        std::span<const hal::PhysRegion> regions);

    [[nodiscard]] bool rides_io_(svc::IIoCoworker& lane, const char* what) noexcept;

    void grant_pause_(hal::Seat seat, xthread::PauseLatch& latch) noexcept;

    app::EventQueue events_;
    app::EventQueue owner_events_;
    os::MonotonicClock clock_;
    app::VideoPump video_pump_;
    app::BtPump bt_pump_;
    app::LinkTxChannel input_inbox_;
    app::LinkRxChannel input_rx_;
    app::LinkTxChannel ui_inbox_;

    xthread::WakeFlag main_wake_;

    app::LinkTxChannel main_inbox_;
    app::LinkRxChannel main_rx_;
    app::LinkRxChannel ui_rx_;

    app::LinkRouter link_router_;
    app::InputPipeline input_pipeline_;
    app::InputMain input_main_;

    app::InputSample input_sample_;
    app::CmdRouter router_;
    std::optional<app::CmdFifo> fifo_;
    std::unique_ptr<reactor::FrameClock> frame_;
    std::optional<reactor::FrameMain> frame_main_;
    svc::ChdPrefetch prefetch_;
    svc::PrefetchMain prefetch_main_;
    app::PcmRingFeeder pcm_feeder_;
    app::PcmMain pcm_main_;
    svc::IoMain io_main_;
    svc::StorageService storage_;
    svc::DiscReadService discs_;
    std::optional<svc::DiscStore> disc_store_;
    std::optional<app::DurableWriteService> writes_;
    app::DevMemWindowMap io_window_map_{};
    std::optional<app::WindowJobService> window_jobs_;
    app::ScreenshotQueue shots_;
    app::ScreenshotPump shot_pump_;
    std::optional<app::ScalerFrameSource> frame_src_;
    proto::OsdSurface surface_;
    app::OsdWire osd_wire_;
    app::ConfigCell config_cell_;
    app::ConfStrCell conf_str_cell_;
    reactor::CoreState core_state_{};
    const app::SupervisorParts parts_;
    app::LinkSession session_;

    hal::HdmiIntLevel hdmi_level_;
    ThreadAssembly assembly_;
    ThreadAssembly::SeatMains seat_mains_{};

    app::BitstreamProgrammer bitstream_;
    app::DevMemWindowMap window_map_{};
    app::SessionOwner owner_;
    ProcessMain process_main_;
    NullOsdClose null_osd_;
    TastySink tasty_sink_;

    app::FrameDemandCell frame_demand_{};
    app::CoreFrameCell core_frames_{};
    app::CoreFrameCounter frame_counter_;

    app::ReplayRing replay_ring_{};
    app::ReplayControlCell replay_control_{};
    app::ReplayStatusCell replay_status_{};
    app::ReplayGate replay_gate_;
    app::ReplayFeeder replay_feeder_;

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
    app::CaptureMain capture_main_;
    app::EncodeMain encode_main_;
    app::RecWriteMain rec_write_main_;
    app::RecorderControl recorder_;

    reactor::RoundTimer round_timer_;
    LadderScope ladder_;
    app::RtMain rt_main_;

    const svc::Vfs* vfs_ = nullptr;
    std::string_view sd_block_;
};

}  // namespace mister::fw
