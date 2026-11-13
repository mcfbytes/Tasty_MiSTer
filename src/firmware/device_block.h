// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <sys/epoll.h>

#include <memory>
#include <optional>
#include <span>
#include <type_traits>

#include "app/bay_releases.h"
#include "app/bitstream_programmer.h"
#include "app/built_core.h"
#include "app/board_ops.h"
#include "app/cheat_blob_cell.h"
#include "app/cheat_catalog_cell.h"
#include "app/cheat_link.h"
#include "app/cmd_fifo.h"
#include "app/companion_host.h"
#include "app/conf_str_cell.h"
#include "app/core_frame_counter.h"
#include "app/core_input_binding.h"
#include "app/core_input_facts.h"
#include "app/core_frame_record.h"
#include "app/dev_mem_window_map.h"
#include "app/event.h"
#include "app/frame_demand.h"
#include "app/capture_arm.h"
#include "app/capture_hit.h"
#include "app/input_gates_cell.h"
#include "app/input_main.h"
#include "app/input_pipeline.h"
#include "app/host_led_watch.h"
#include "app/identity_record.h"
#include "app/ini_parse.h"
#include "app/launcher_demand.h"
#include "app/launcher_state.h"
#include "app/link_router.h"
#include "app/link_rx_channel.h"
#include "app/link_session.h"
#include "app/joymap_ends.h"
#include "app/key_focus.h"
#include "app/link_tx_channel.h"
#include "app/lw_osd_endpoint.h"
#include "app/lw_sector_endpoint.h"
#include "app/lw_sys_endpoint.h"
#include "app/raw_key_edge.h"
#include "app/rumble_players_cell.h"
#include "app/disk_relay.h"
#include "app/save_upload_relay.h"
#include "app/mailbox_relay.h"
#include "app/osd_grant.h"
#include "app/osd_route.h"
#include "app/osd_wire.h"
#include "app/pcm_main.h"
#include "app/pcm_ring_feeder.h"
#include "app/save_state_attach.h"
#include "app/save_state_facts.h"
#include "app/scaler_frame_source.h"
#include "app/screenshot_queue.h"
#include "app/sector_grant.h"
#include "app/sector_route.h"
#include "app/session_owner.h"
#include "app/supervisor_parts.h"
#include "app/ui_write_lane.h"
#include "app/video_pump.h"
#include "app/window_job_service.h"
#include "boot_parts.h"
#include "cores/save_copy_level.h"
#include "hal/fpga_memory.h"
#include "hal/hdmi_int_level.h"
#include "hal/thread_map.h"
#include "infra/coalesced_kick.h"
#include "infra/diag_log.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/park_fds.h"
#include "infra/polled.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "infra/unique_fd.h"
#include "infra/wake_flag.h"
#include "board_reset.h"
#include "ladder_scope.h"
#include "open_verdict.h"
#include "os/clock.h"
#include "os/epoll_set.h"
#include "os/monotonic_clock.h"
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
    struct Parts {

        struct Wires {
            xthread::DiagLog& diag;
            os::MonotonicClock& clock;
            xthread::WakeFlag& main_wake;
            xthread::WakeFlag& main_stop;
            xthread::WakeFlag& ui_wake;
            xthread::WakeFlag& io_wake;
            app::LinkTxChannel<SeatTag::Ui>& ui_inbox;
            app::VideoPump& video;

            app::KeyFocusCell& key_focus;
            app::CoreInputBinding& core_input;
            const app::JoyMapEnds& joymap;
            app::MenuKeyInbox& menu_keys;
            app::OsdWire& osd;
            app::EventQueue<SeatTag::RT>& events;
            app::EventQueue<SeatTag::Main>& owner_events;
            app::CoreFrameCounter& frames;
            app::InputEmit& input_emit;

            infra::OptRef<const reactor::FrameCell> vsync{};
        };

        struct Cells {
            app::ConfigCell& config;
            app::ConfStrCell& conf_str;
            app::CheatBlobCell& cheat_blob;
            app::CheatCatalogCell& cheat_catalog;
            app::CheatLink& cheat_link;
            app::SaveStateAttach::AskInbox& states_asks;
            app::SaveStateFactsCell& states_facts;
            cores::SaveCopyCell& save_copy;
            app::DevMemWindowMap<seat_owner::Io>& io_windows;
            app::DevMemWindowMap<seat_owner::Ui>& ui_windows;
            app::FrameDemandCell& frame_demand;
            app::CoreFrameCell& core_frames;

            app::IniCell& ini;

            xthread::WakeFlag& disk_activity;
        };

        struct SessionFacts {
            app::LinkSession& link;
            app::SessionOwner& owner;
        };

        struct Frontend {
            infra::OptRef<app::LauncherDemandCell> launcher_demand{};
            infra::OptRef<xthread::WakeFlag> launcher_wake{};
            infra::OptRef<const app::LauncherStateCell> launcher_state{};
            infra::OptRef<app::LauncherKeyBridge> launcher_keys{};
            bool grants_uart = false;
        };

        struct Seats {
            ThreadAssembly::SeatMains& mains;
            DiagSampler& diag;

            infra::OptRef<app::SaveStateAttach> save_states{};

            infra::OptRef<app::UiWriteLane> ui_writes{};
        };

        Wires wires;
        Cells cells;
        SessionFacts session;
    };

    [[nodiscard]] Parts parts() noexcept;

    [[nodiscard]] infra::OptRef<app::UartModeController::Handoffs> uart_handoffs() noexcept {
        return parts_.uart_handoffs;
    }

    DeviceBlock(const BootParts& plat, app::IStopSignal& stop, const Parts::Frontend& front);
    DeviceBlock(const DeviceBlock&) = delete;
    DeviceBlock& operator=(const DeviceBlock&) = delete;

    void open_wires(const BootParts& plat) noexcept;

    void open_seats(const BootParts& plat, const Parts::Seats& seats) noexcept;

    void grant_pauses(const ThreadAssembly::SeatMains& mains, infra::OptRef<UiMain> ui) noexcept;

    void grant_pause(SeatTag seat, xthread::PauseLatch& latch) noexcept;

    [[nodiscard]] DiagSampler::DeviceCells diag_sources(const BootParts& plat) noexcept;

    [[nodiscard]] DiagSampler::Services diag_services() noexcept;

    OpenVerdict& verdict() noexcept { return opened_; }

    [[nodiscard]] const OpenVerdict& verdict() const noexcept { return opened_; }

    std::optional<app::CmdFifo> open_cmd_fifo(
        const BootParts& plat,
        app::ICmdVerbSink& route
    ) noexcept;

private:
    static std::unique_ptr<reactor::FrameClock> open_frame_clock(
        const char* vsync_device,
        const os::IClock& clock,
        OpenVerdict& verdict
    );

    [[nodiscard]] std::optional<svc::PrefetchMain> open_prefetch_main_();
    [[nodiscard]] std::optional<app::PcmMain> open_pcm_main_();
    [[nodiscard]] std::optional<svc::IoMain> open_io_main_();
    [[nodiscard]] std::optional<os::EpollSet> open_io_doorbells_();
    [[nodiscard]] infra::OptRef<const os::EpollSet> io_doorbell_set_() noexcept;

    [[nodiscard]] app::SupervisorParts::Services services_() noexcept;

    [[nodiscard]] std::optional<app::ScalerFrameSource> open_frame_source_(
        const BootParts& plat
    ) noexcept;
    [[nodiscard]] std::optional<app::InputPipeline> open_input_pipeline_(
        const Parts::Frontend& front
    ) noexcept;

    [[nodiscard]] std::optional<app::HostLedWatch> open_led_watch_(const BootParts& plat) noexcept;
    [[nodiscard]] std::optional<app::InputMain> open_input_main_() noexcept;

    [[nodiscard]] bool rides_io_(svc::IIoCoworker& lane, const char* what) noexcept;
    template <class Row>
    void grant_if_latched_(const ThreadAssembly::SeatMains& mains) noexcept;
    template <class... Rows>
    void grant_latched_(const ThreadAssembly::SeatMains& mains, SeatList<Rows...>) noexcept;

public:
    OpenVerdict opened_{};

    const hal::ThreadMap& threads_;

    xthread::DiagLog diag_log_;

    xthread::Telemetry<bool, SeatTag::RT> transitioning_{};

    xthread::WakeFlag io_wake_;

    xthread::WakeFlag disk_activity_;

    std::optional<os::EpollSet> io_doorbells_;

    app::SectorGrantCell sector_grant_{};
    app::SectorRouteCell sector_route_{};
    app::SectorLeaseCell sector_lease_{};

    app::OsdGrantCell osd_grant_{};
    app::OsdRouteCell osd_route_{};
    app::OsdLeaseCell osd_lease_{};

    app::SysGrantCell sys_grant_{};
    app::SysRouteCell sys_route_{};
    app::SysLeaseCell sys_lease_{};
    proto::StatusSnapshotCell status_snapshots_{};

    svc::DiscReadService::GeomCell disc_geometry_{};
    svc::DiscReadService::CountCell disc_counters_{};

    svc::UnboundDisc unbound_discs_{};

    app::DiskRelay disk_relay_;

    app::SaveUploadRelay save_relay_;

    hal::PinLevelCell pin_levels_{};

    hal::HdmiIntLevel hdmi_level_{pin_levels_};

    app::EventQueue<SeatTag::RT> events_;
    app::EventQueue<SeatTag::Main> owner_events_;
    static_assert(
        !std::is_same_v<decltype(events_), decltype(owner_events_)>,
        "the two EventQueues are one per producer seat and never interchangeable"
    );
    os::MonotonicClock clock_;

    app::LinkTxChannel<SeatTag::Ui> ui_inbox_;

    xthread::WakeFlag edge_reset_;
    xthread::WakeFlag hotplug_;
    xthread::WakeFlag input_ctrl_;
    app::KeyFocusCell key_focus_;
    app::InputGatesCell gates_cell_;
    xthread::Counter activity_;
    app::CoreInputCell core_facts_;
    app::CaptureArmCell capture_arm_;
    app::CaptureHitCell capture_hit_;
    app::RumblePlayersCell rumble_players_;

    app::CoreInputBinding core_input_{
        app::CoreInputBinding::Ends{core_facts_, edge_reset_, hotplug_, input_ctrl_.kicker()}
    };
    app::JoyMapEnds joymap_{
        .arm = capture_arm_,
        .hit = capture_hit_,
        .hotplug = hotplug_,
        .input_ctrl = input_ctrl_.kicker()
    };
    app::IniCell ini_cell_{};
    app::LinkTxChannel<SeatTag::Input> input_inbox_;
    app::LinkRxChannel<SeatTag::Input> input_rx_;

    xthread::WakeFlag main_wake_;

    xthread::WakeFlag main_stop_{};

    app::LinkTxChannel<SeatTag::Main> main_inbox_;
    app::LinkRxChannel<SeatTag::Main> main_rx_;
    app::LinkRxChannel<SeatTag::Ui> ui_rx_;

    xthread::WakeFlag ui_wake_;

    app::MenuKeyInbox menu_keys_{ui_wake_};
    app::UartModeController::Handoffs uart_handoffs_{ui_wake_};

    app::LinkRouter link_router_;

    app::VideoPump video_pump_;

    app::InputEmit input_emit_;
    app::InputBuild input_build_;
    xthread::WakeFlag input_wake_{};

    xthread::CoalescedKick<SeatTag::Input, SeatTag::RT> rt_input_kick_;

    std::optional<app::HostLedWatch> led_watch_;
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
    app::LwSectorEndpoint lw_sector_;
    app::LwSysEndpoint lw_sys_;
    app::DevMemWindowMap<seat_owner::Io> io_window_map_{};
    app::WindowJobService window_jobs_;
    app::ScreenshotQueue shots_;
    std::optional<app::ScalerFrameSource> frame_src_;
    proto::OsdSurface surface_;
    app::OsdWire osd_wire_;
    app::LwOsdEndpoint lw_osd_;
    app::ConfigCell config_cell_;

    app::IdentityRecordCell identity_record_cell_;
    app::ConfStrCell conf_str_cell_;

    app::LadderStateCell ladder_cell_{};
    reactor::CoreState core_state_{};

    app::CheatBlobCell cheat_blob_{};

    app::SaveStateAttach::AskInbox save_state_asks_{xthread::Polled{}};
    app::SaveStateFactsCell save_state_facts_{};
    cores::SaveCopyCell save_copy_{};

    app::CheatCatalogCell cheat_catalog_{};
    app::CheatLink cheat_link_{cheat_blob_};

    app::CoreBay bay_{};
    app::BayReleasesCell bay_releases_{};

    LadderScope ladder_;

    BoardReset board_reset_;

    xthread::MainLogLane main_log_lane_{};
    const app::SupervisorParts parts_;
    app::LinkSession session_;

    app::BitstreamProgrammer bitstream_;
    app::DevMemWindowMap<seat_owner::Main> window_map_{};
    app::DevMemWindowMap<seat_owner::Ui> ui_window_map_{};
    static_assert(
        !std::is_same_v<decltype(window_map_), decltype(io_window_map_)>
            && !std::is_same_v<decltype(window_map_), decltype(ui_window_map_)>
            && !std::is_same_v<decltype(io_window_map_), decltype(ui_window_map_)>,
        "one window map per mapping seat; no instance is shared between two"
    );
    app::SessionOwner owner_;

    app::FrameDemandCell frame_demand_{};
    app::CoreFrameCell core_frames_{};
    app::CoreFrameCounter frame_counter_;

    const svc::Vfs& vfs_;
};

}
