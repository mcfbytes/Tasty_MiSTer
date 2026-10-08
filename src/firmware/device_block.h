// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <span>

#include "app/bitstream_programmer.h"
#include "app/board_ops.h"
#include "app/bt_pump.h"
#include "app/cheat_blob_cell.h"
#include "app/cheat_link.h"
#include "app/cmd_fifo.h"
#include "app/companion_host.h"
#include "app/conf_str_cell.h"
#include "app/core_frame_counter.h"
#include "app/core_frame_record.h"
#include "app/dev_mem_window_map.h"
#include "app/durable_write_service.h"
#include "app/event.h"
#include "app/frame_demand.h"
#include "app/input_main.h"
#include "app/input_pipeline.h"
#include "app/ini_parse.h"
#include "app/launcher_demand.h"
#include "app/launcher_state.h"
#include "app/link_router.h"
#include "app/link_rx_channel.h"
#include "app/link_session.h"
#include "app/link_tx_channel.h"
#include "app/mailbox_relay.h"
#include "app/osd_wire.h"
#include "app/pcm_main.h"
#include "app/pcm_ring_feeder.h"
#include "app/scaler_frame_source.h"
#include "app/screenshot_queue.h"
#include "app/session_owner.h"
#include "app/session_seats.h"
#include "app/video_pump.h"
#include "app/window_job_service.h"
#include "boot_parts.h"
#include "hal/fpga_memory.h"
#include "hal/hdmi_int_level.h"
#include "infra/diag_log.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/park_fds.h"
#include "infra/seat.h"
#include "infra/wake_flag.h"
#include "ladder_scope.h"
#include "os/clock.h"
#include "proto/osd_surface.h"
#include "reactor/core_state.h"
#include "reactor/frame_clock.h"
#include "reactor/frame_main.h"
#include "svc/chd_prefetch.h"
#include "svc/disc_read_service.h"
#include "svc/disc_store.h"
#include "svc/io_main.h"
#include "svc/prefetch_main.h"
#include "svc/storage_service.h"
#include "svc/unbound_disc.h"
#include "thread_assembly.h"

namespace mister::fw {

class DeviceBlock {
    TASTY_SEAT_EXEMPT(boot);

public:
    struct Front {
        app::LauncherDemandCell* launcher_demand = nullptr;
        infra::OptRef<xthread::WakeFlag> launcher_wake{};
        infra::OptRef<const app::LauncherStateCell> launcher_state{};
        infra::OptRef<app::LauncherKeyBridge> launcher_keys{};
        bool grants_uart = false;
    };

    struct Seats {
        ThreadAssembly::SeatMains& mains;
        DiagSampler& diag;
    };

    static constexpr const char* kDiagPath = "/tmp/MiSTer_diag";

    DeviceBlock(const BootParts& plat, app::IStopSignal& stop, const Front& front);
    DeviceBlock(const DeviceBlock&) = delete;
    DeviceBlock& operator=(const DeviceBlock&) = delete;

    void open_wires(const BootParts& plat) noexcept;

    void open_seats(const BootParts& plat, const Seats& seats) noexcept;

    void grant_pauses(const ThreadAssembly::SeatMains& mains, UiMain* ui) noexcept;

    void grant_pause(SeatTag seat, xthread::PauseLatch& latch) noexcept;

    [[nodiscard]] DiagSampler::DeviceCells diag_sources(const BootParts& plat) noexcept;

    static std::optional<app::CmdFifo> open_cmd_fifo(app::ICmdVerbSink& route);

private:
    static std::unique_ptr<reactor::FrameClock> open_frame_clock(const char* vsync_device,
                                                                 reactor::Executive& exec);

    [[nodiscard]] static Ex<hal::FpgaMemory> map_scaler_out(
        std::span<const hal::PhysRegion> regions);

    [[nodiscard]] bool rides_io_(svc::IIoCoworker& lane, const char* what) noexcept;
    template <class Row>
    void grant_if_latched_(const ThreadAssembly::SeatMains& mains) noexcept;
    template <class... Rows>
    void grant_latched_(const ThreadAssembly::SeatMains& mains, SeatList<Rows...>) noexcept;

public:
    const Front front_;

    xthread::DiagLog diag_log_;

    std::atomic<bool> transitioning_{false};

    xthread::WakeFlag io_wake_;

    svc::DiscReadService::GeomCell disc_geometry_{};
    svc::DiscReadService::CountCell disc_counters_{};

    svc::UnboundDisc unbound_discs_{};

    app::FileStreamService file_streams_;

    hal::PinLevelCell pin_levels_{};

    hal::HdmiIntLevel hdmi_level_{pin_levels_};
    app::EventQueue events_;
    app::EventQueue owner_events_;
    os::MonotonicClock clock_;

    app::LinkTxChannel ui_inbox_;
    app::InputWire input_wire_;
    app::VideoPump video_pump_;
    app::BtPump bt_pump_;
    app::LinkTxChannel input_inbox_;
    app::LinkRxChannel input_rx_;

    xthread::WakeFlag main_wake_;

    app::LinkTxChannel main_inbox_;
    app::LinkRxChannel main_rx_;
    app::LinkRxChannel ui_rx_;

    xthread::WakeFlag ui_wake_;
    app::UartModeController::Handoffs uart_handoffs_{ui_wake_};

    app::LinkRouter link_router_;

    app::InputEmit input_emit_;
    app::InputBuild input_build_;
    xthread::WakeFlag input_wake_{};
    std::optional<app::InputPipeline> input_pipeline_;
    std::optional<app::InputMain> input_main_;
    std::unique_ptr<reactor::FrameClock> frame_;
    std::optional<reactor::FrameMain> frame_main_;
    svc::ChdPrefetch prefetch_;
    std::optional<svc::PrefetchMain> prefetch_main_;

    xthread::WakeFlag pcm_wake_;
    app::PcmRingFeeder::Commands pcm_commands_{pcm_wake_};
    app::PcmRingFeeder pcm_feeder_;
    app::MailboxRelay mailbox_relay_;
    app::CompanionHost companion_host_;
    std::optional<app::PcmMain> pcm_main_;
    std::optional<svc::IoMain> io_main_;
    svc::StorageService storage_;
    svc::DiscStore disc_store_;
    svc::DiscReadService discs_;
    std::optional<app::DurableWriteService> writes_;
    app::DevMemWindowMap io_window_map_{};
    std::optional<app::WindowJobService> window_jobs_;
    app::ScreenshotQueue shots_;
    std::optional<app::ScalerFrameSource> frame_src_;
    proto::OsdSurface surface_;
    app::OsdWire osd_wire_;
    app::ConfigCell config_cell_;
    app::ConfStrCell conf_str_cell_;

    app::LadderStateCell ladder_cell_{};
    reactor::CoreState core_state_{};

    app::CheatBlobCell cheat_blob_{};
    app::CheatLink cheat_link_{cheat_blob_};

    LadderScope ladder_;
    const app::SupervisorParts parts_;
    app::LinkSession session_;

    app::BitstreamProgrammer bitstream_;
    app::DevMemWindowMap window_map_{};
    app::SessionOwner owner_;

    app::FrameDemandCell frame_demand_{};
    app::CoreFrameCell core_frames_{};
    app::CoreFrameCounter frame_counter_;

    const svc::Vfs* vfs_ = nullptr;
};

}  // namespace mister::fw
