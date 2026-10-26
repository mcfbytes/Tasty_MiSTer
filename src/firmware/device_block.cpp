// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_block.h"

#include <cstdio>
#include <string>
#include <utility>

#include "app/sector_route.h"
#include "cores/disk_servant.h"
#include "cores/mailbox_servants.h"
#include "infra/opt_ref.h"
#include "os/bt_probe.h"
#include "reactor/notifier.h"
#include "report.h"

namespace mister::fw {

using namespace mister;

bool DeviceBlock::rides_io_(svc::IIoCoworker& lane, const char* what) noexcept {
    if (!io_main_)
        return false;
    if (io_main_->add_coworker(&lane))
        return true;
    report(
        what,
        Error{Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(svc::IoMain::kMaxCoworkers)}
    );
    return false;
}

std::optional<app::CmdFifo> DeviceBlock::open_cmd_fifo(app::ICmdVerbSink& route) {
    std::optional<app::CmdFifo> fifo;
    if (auto f = app::CmdFifo::open(app::kCmdFifoPath, route); f) {
        fifo.emplace(std::move(*f));
    } else {
        report("CmdFifo::open (degraded: no /dev/MiSTer_cmd)", f.error());
    }
    return fifo;
}

Ex<hal::FpgaMemory> DeviceBlock::map_scaler_out(std::span<const hal::PhysRegion> regions) {

    if (regions.size() != hal::kRegionCount) {
        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), 0});
    }

    return hal::FpgaMemory::map(
        regions[static_cast<std::size_t>(hal::RegionId::ScalerOut)],
        os::MmioRegion::Access::ReadOnly
    );
}

std::optional<app::ScalerFrameSource> DeviceBlock::open_frame_source_(
    const BootParts& plat
) noexcept {
    if (auto w = map_scaler_out(plat.regions); w) {
        return std::optional<app::ScalerFrameSource>{
            std::in_place,
            std::move(*w),
            video_pump_.geometry_cell()
        };
    } else {
        report("FpgaMemory::map(scaler-out) (degraded: no screenshots)", w.error());
        return std::nullopt;
    }
}

std::optional<app::InputPipeline> DeviceBlock::open_input_pipeline_(
    const BootParts& plat
) noexcept {
    auto fds = app::InputDecode::Fds::create(input_wire_);
    if (!fds) {
        report("InputPipeline::open (degraded: input unavailable)", fds.error());
        return std::nullopt;
    }
    return std::optional<app::InputPipeline>{
        std::in_place,
        std::move(*fds),
        input_wire_,
        input_build_,
        input_emit_,
        clock_,
        app::InputDecode::Wiring{
            .diag = diag_log_,
            .inbox = input_inbox_,
            .link_rx = input_rx_,
            .rt_wake = plat.exec.kicker(),
            .cell = front_.launcher_state,
            .keys = front_.launcher_keys,
            .ui_wake = infra::OptRef<xthread::WakeFlag>{ui_wake_},
        }
    };
}

std::optional<app::InputMain> DeviceBlock::open_input_main_() noexcept {
    if (!input_pipeline_)
        return std::nullopt;
    auto park = xthread::ParkFds::create(input_wake_);
    if (!park) {
        report("InputMain::open (degraded: input unavailable)", park.error());
        return std::nullopt;
    }
    if (auto w = input_pipeline_->decode().watch(*park); !w) {
        report("InputMain::open (degraded: input unavailable)", w.error());
        return std::nullopt;
    }
    return std::optional<app::InputMain>{
        std::in_place,
        input_pipeline_->decode(),
        std::move(*park),
        infra::OptRef{main_wake_}
    };
}

std::unique_ptr<reactor::FrameClock> DeviceBlock::open_frame_clock(
    const char* vsync_device,
    reactor::Executive& exec
) {
    std::unique_ptr<reactor::FrameClock> frame;
    if (vsync_device == nullptr) {
        std::fprintf(stderr, "mister: this board row names no vsync device: no T-FRAME\n");
        return frame;
    }
    if (auto fc = reactor::FrameClock::open(vsync_device); fc) {
        frame = std::move(*fc);
        if (auto nf = reactor::Notifier::frame(frame->eventfd()); nf) {
            if (auto a = exec.add_notifier(std::move(*nf)); !a) {
                report("Executive::add_notifier(frame)", a.error());
            }
        } else {
            report("Notifier::frame", nf.error());
        }
    } else {
        std::fprintf(stderr, "mister: vsync device %s\n", vsync_device);
        report("FrameClock::open (degraded: no T-FRAME)", fc.error());
    }
    return frame;
}

DiagSampler::DeviceCells DeviceBlock::diag_sources(const BootParts& plat) noexcept {
    return DiagSampler::DeviceCells{
        .mgl_row0 = owner_.mgl_row0_cell(),
        .window_counts = owner_.window_cell(),
        .bay_counts = owner_.core_builder().counts_cell(),
        .diag = session_.diag_cell(),
        .pause_expiries = owner_.pause_expiries_cell(),
        .recover_polls = owner_.recover_polls_cell(),
        .save_write_failures = owner_.save_write_failures_cell(),
        .fallbacks = owner_.fallback_cell(),
        .ini_write_failures = owner_.ini_write_failures_cell(),
        .doorbell = session_.doorbell_cell(),
        .round_timing = plat.round_timer.cell(),

        .frames = infra::opt_ref_of(frame_ != nullptr ? &frame_->frame_cell() : nullptr),
        .screenshots = shots_,
        .sector_route = infra::OptRef<const app::SectorRouteCell>{sector_route_},
        .osd_route = infra::OptRef<const app::OsdRouteCell>{osd_route_},
        .sys_route = infra::OptRef<const app::SysRouteCell>{sys_route_},
    };
}

void DeviceBlock::grant_pause(SeatTag seat, xthread::PauseLatch& latch) noexcept {
    owner_.grant_pause(seat, latch);
}

template <class M>
concept HasPauseLatch = requires(M& m) { m.pause_latch(); };

template <class Row>
void DeviceBlock::grant_if_latched_(const ThreadAssembly::SeatMains& mains) noexcept {
    using M = typename Row::Main;
    if constexpr (HasPauseLatch<M>) {
        if (M* m = mains.mains.get<M>(); m != nullptr)
            grant_pause(Row::seat, m->pause_latch());
    }
}

template <class... Rows>
void DeviceBlock::grant_latched_(
    const ThreadAssembly::SeatMains& mains,
    SeatList<Rows...>
) noexcept {
    (grant_if_latched_<Rows>(mains), ...);
}

void DeviceBlock::grant_pauses(const ThreadAssembly::SeatMains& mains, UiMain* ui) noexcept {
    if (ui != nullptr)
        grant_pause(SeatTag::Ui, ui->pause_latch());
    static_assert(
        HasPauseLatch<app::InputMain>
            && HasPauseLatch<app::PcmMain>
            && HasPauseLatch<svc::PrefetchMain>
            && HasPauseLatch<svc::IoMain>,
        "a renamed pause_latch() would silently drop that seat's grant"
    );
    grant_latched_(mains, SeatMainList{});
}

DeviceBlock::DeviceBlock(const BootParts& plat, app::IStopSignal& stop, const Front& front)
    : front_(front),
      file_streams_{io_wake_, infra::OptRef<const svc::Vfs>{plat.vfs}},
      events_(),
      owner_events_(),
      clock_(),
      input_wire_{},
      video_pump_{
          plat.vfs,
          clock_,
          plat.video,
          {},
          hdmi_level_,
          app::VideoPump::Wiring{
              .activity = input_wire_,
              .link_tx = ui_inbox_,
          }
      },
      bt_pump_{clock_, &os::bluetooth_adapter_up, infra::OptRef<xthread::DiagLog>{diag_log_}},
      main_rx_{main_wake_},
      link_router_{main_rx_, ui_rx_, input_rx_},
      input_emit_{input_wire_, plat.link, link_router_},
      input_build_{plat.vfs, input_wire_},
      input_pipeline_(open_input_pipeline_(plat)),
      input_main_(open_input_main_()),
      frame_(open_frame_clock(plat.video.vsync_device, plat.exec)),
      prefetch_(),
      prefetch_main_(open_prefetch_main(prefetch_, main_wake_)),
      pcm_feeder_{clock_, pcm_commands_},
      mailbox_relay_{pcm_wake_},
      companion_host_{pcm_wake_},
      pcm_main_(open_pcm_main(
          pcm_wake_,
          pcm_feeder_,
          pcm_commands_,
          mailbox_relay_,
          companion_host_,
          main_wake_
      )),
      io_main_(open_io_main(io_wake_, main_wake_, io_doorbells_.get())),
      storage_(io_wake_),
      disc_store_{plat.vfs, disc_geometry_, disc_counters_, prefetch_},
      discs_(
          io_wake_,
          {.geometry = disc_geometry_,
           .counters = disc_counters_,
           .reader = infra::OptRef<svc::IDiscReader>{disc_store_},
           .mounter = infra::OptRef<svc::IDiscMounter>{disc_store_}}
      ),
      lw_sector_(
          app::LwSectorEndpoint::Wiring{
              .grant = sector_grant_,
              .route = sector_route_,
              .geometry = disc_geometry_,
              .discs = discs_,
              .doorbells = io_doorbells_.get(),
              .saves = infra::OptRef<const svc::ISaveFilesOnIo>{storage_}
          }
      ),
      lw_sys_(
          app::LwSysEndpoint::Wiring{
              .grant = sys_grant_,
              .route = sys_route_,
              .snapshots = status_snapshots_,
              .doorbells = io_doorbells_.get()
          }
      ),
      writes_(plat.vfs, io_wake_),
      window_jobs_(plat.vfs, io_window_map_, plat.fpga_mem.region, io_wake_),
      shots_(),
      frame_src_(open_frame_source_(plat)),
      surface_(),
      osd_wire_{surface_, plat.link},
      lw_osd_(
          app::LwOsdEndpoint::Wiring{.grant = osd_grant_, .route = osd_route_, .surface = surface_}
      ),
      ladder_{transitioning_, infra::OptRef<reactor::RoundTimer>{plat.round_timer}},
      board_reset_{plat.bridges},
      parts_{
          .video = video_pump_.wire(),
          .input = input_wire_,
          .osd = osd_wire_,
          .link_inbox = main_inbox_,
          .link_rx = main_rx_,
          .config_cell = config_cell_,
          .ui_inbox = ui_inbox_,
          .input_inbox = input_inbox_,
          .router = link_router_,
          .ladder_cell = ladder_cell_,
          .streams = file_streams_,
          .unbound_discs = unbound_discs_.service,
          .pin_levels = pin_levels_,
          .main_wake = main_wake_,
          .exec = plat.exec,
          .core_state = core_state_,
          .clock = clock_,

          .input_emitter = input_emit_.emitter(),

          .log_lane = plat.rt_lane,
          .cheats = cheat_link_,
          .bay = bay_,
          .bay_releases = bay_releases_,
          .vfs = infra::OptRef<const svc::Vfs>{plat.vfs},
          .uart_handoffs = infra::opt_ref_of(front.grants_uart ? &uart_handoffs_ : nullptr),
          .stdout_routing = true,
          .doorbells = plat.doorbells,
          .fpga_mem = plat.fpga_mem,
          .lw_window = plat.lw_window,
          .doorbell_nodes = plat.doorbell_nodes,
          .doorbell_cpus = hal::cpus_off_rt(plat.threads),

          .status_snapshots = io_main_ && io_doorbells_.get() >= 0
              ? infra::OptRef<const proto::StatusSnapshotCell>{status_snapshots_}
              : infra::OptRef<const proto::StatusSnapshotCell>{},

          .storage_channel = infra::OptRef<proto::IStorageChannel>{storage_},

          .board_ops =
              {.rt = infra::OptRef<app::IRtPriorityScope>{ladder_},
               .stop = infra::OptRef<app::IStopSignal>{stop}},
          .save_copy = infra::OptRef<const cores::SaveCopyCell>{save_copy_},
          .services = services_(),
      },
      session_{plat.link, plat.link, plat.link, plat.fpga_mem.region, events_, parts_},
      bitstream_{plat.programmer, plat.bridges, plat.vfs, plat.program},
      owner_{
          session_.quiesce_channel(),
          app::SessionOwner::Link{
              .inbox = main_inbox_,
              .rx = main_rx_,
              .config_cell = config_cell_,
              .owner_events = owner_events_,
              .conf_str_cell = conf_str_cell_,
              .cheat_catalog_cell = cheat_catalog_,
              .ladder_cell = ladder_cell_,
              .main_wake = main_wake_,

              .ops = {.reset = infra::OptRef<app::IBoardReset>{board_reset_}},
              .boot_handoff = plat.programmer.boot_handoff() != nullptr
                  ? infra::OptRef<hal::IBootHandoff>{*plat.programmer.boot_handoff()}
                  : infra::OptRef<hal::IBootHandoff>{},
              .launcher_demand = front.launcher_demand,
              .launcher_wake = front.launcher_wake,
              .programmer = bitstream_,
              .storage = plat.vfs,
              .window_map = window_map_,
              .fabric_cell = session_.fabric_cell(),
              .status_cell = session_.status_cell(),
              .mount_status_cell = session_.mount_status_cell(),
              .save_extent_cell = session_.save_extent_cell(),
              .file_tx_level_cell = session_.file_tx_level_cell(),
              .pin_levels = pin_levels_,
              .save_state_asks = infra::OptRef{save_state_asks_},
              .save_state_facts = infra::OptRef{std::as_const(save_state_facts_)},
              .bay = bay_,
              .bay_releases = bay_releases_,
              .core_terms = session_.core_terms(),
              .companion_binds = pcm_main_
                  ? infra::OptRef<app::CompanionHost::Binds>{companion_host_.binds()}
                  : infra::OptRef<app::CompanionHost::Binds>{},

              .sector_grant = io_main_ && io_doorbells_.get() >= 0
                  ? infra::OptRef<app::SectorGrantCell>{sector_grant_}
                  : infra::OptRef<app::SectorGrantCell>{},
              .sector_route = infra::OptRef<const app::SectorRouteCell>{sector_route_},
              .osd_grant = infra::OptRef<app::OsdGrantCell>{osd_grant_},
              .osd_route = infra::OptRef<const app::OsdRouteCell>{osd_route_},

              .sys_grant = io_main_ && io_doorbells_.get() >= 0
                  ? infra::OptRef<app::SysGrantCell>{sys_grant_}
                  : infra::OptRef<app::SysGrantCell>{},
              .sys_route = infra::OptRef<const app::SysRouteCell>{sys_route_},
              .io_wake = io_main_ ? infra::OptRef<xthread::WakeFlag>{io_wake_}
                                  : infra::OptRef<xthread::WakeFlag>{},
              .aperture = plat.fpga_mem.region
          }
      },
      frame_counter_{app::CoreFrameCounter::Wiring{
          .link = plat.link,
          .clock = clock_,
          .demand = infra::OptRef<const app::FrameDemandCell>{frame_demand_},
          .out = infra::OptRef<app::CoreFrameCell>{core_frames_}
      }},
      vfs_(plat.vfs) {

    static_assert(svc::IoMain::kMaxCoworkers >= 9);

    if (pcm_main_)
        companion_host_.install(cores::make_servants(vfs_));

    if (io_main_)
        disk_relay_.install(cores::make_disk_servant(vfs_));
}

std::optional<svc::PrefetchMain> DeviceBlock::open_prefetch_main(
    svc::ChdPrefetch& prefetch,
    xthread::WakeFlag& main_wake
) {
    if (auto po = prefetch.open(); !po) {
        report("ChdPrefetch::open (degraded: CHD decode stays inline on T-IO)", po.error());
    } else if (auto fds = xthread::ParkFds::create(prefetch.wake()); !fds) {
        report("PrefetchMain::open (degraded: CHD decode stays inline on T-IO)", fds.error());
    } else {
        return std::optional<svc::PrefetchMain>{
            std::in_place,
            prefetch,
            std::move(*fds),
            infra::OptRef{main_wake}
        };
    }
    return std::nullopt;
}

std::optional<app::PcmMain> DeviceBlock::open_pcm_main(
    xthread::WakeFlag& pcm_wake,
    app::PcmRingFeeder& feeder,
    app::PcmRingFeeder::Commands& commands,
    app::MailboxRelay& relay,
    app::CompanionHost& host,
    xthread::WakeFlag& main_wake
) {
    if (auto fds = xthread::ParkFds::create(pcm_wake); !fds) {
        report("PcmMain::open (degraded: no T-PCM, so no fed window)", fds.error());
        return std::nullopt;
    } else {
        return std::optional<app::PcmMain>{
            std::in_place,
            feeder,
            std::move(*fds),
            commands,
            app::PcmMain::Mailbox{relay, host},
            infra::OptRef{main_wake}
        };
    }
}

std::optional<svc::IoMain> DeviceBlock::open_io_main(
    xthread::WakeFlag& io_wake,
    xthread::WakeFlag& main_wake,
    int doorbells
) {
    if (auto fds = xthread::ParkFds::create(io_wake); !fds) {
        report("IoMain fds (degraded: no T-IO, saves reach no file)", fds.error());
        return std::nullopt;
    } else {
        return std::optional<svc::IoMain>{
            std::in_place,
            std::move(*fds),
            infra::OptRef{main_wake},
            doorbells
        };
    }
}

app::SupervisorParts::Services DeviceBlock::services_() noexcept {
    using app::SupervisorParts;
    const bool io = io_main_.has_value();
    return SupervisorParts::Services{
        .feeder = pcm_main_ ? infra::OptRef<app::PcmRingFeeder>{pcm_feeder_}
                            : infra::OptRef<app::PcmRingFeeder>{},
        .prefetch = prefetch_main_ ? infra::OptRef<svc::ChdPrefetch>{prefetch_}
                                   : infra::OptRef<svc::ChdPrefetch>{},
        .mailbox = pcm_main_ ? infra::OptRef<app::MailboxRelay>{mailbox_relay_}
                             : infra::OptRef<app::MailboxRelay>{},
        .disks = io ? infra::OptRef<app::DiskRelay>{disk_relay_} : infra::OptRef<app::DiskRelay>{},
        .discs = io ? infra::OptRef<svc::DiscReadService>{discs_}
                    : infra::OptRef<svc::DiscReadService>{},
        .writes = io ? infra::OptRef<app::DurableWriteService>{writes_}
                     : infra::OptRef<app::DurableWriteService>{},
        .window_jobs = io ? infra::OptRef<app::WindowJobService>{window_jobs_}
                          : infra::OptRef<app::WindowJobService>{},
        .storage_life = io ? infra::OptRef<svc::IStorageLifecycle>{storage_}
                           : infra::OptRef<svc::IStorageLifecycle>{},
    };
}

DiagSampler::Services DeviceBlock::diag_services() noexcept {
    return DiagSampler::Services{
        .frame_source = frame_src_ ? infra::OptRef<app::IFrameSource>{*frame_src_}
                                   : infra::OptRef<app::IFrameSource>{},
        .input_sample = input_main_
            ? infra::OptRef<const app::InputSample>{input_pipeline_->sample()}
            : infra::OptRef<const app::InputSample>{},
        .input_build = input_main_ ? infra::OptRef<app::InputBuild>{input_build_}
                                   : infra::OptRef<app::InputBuild>{},
        .prefetch = prefetch_main_ ? infra::OptRef<svc::ChdPrefetch>{prefetch_}
                                   : infra::OptRef<svc::ChdPrefetch>{},
    };
}

void DeviceBlock::open_wires() noexcept {
    (void)diag_log_.open(kDiagPath);
    diag_log_.set_echo_stderr(true);
}

void DeviceBlock::open_seats(const BootParts& plat, const Seats& seats) noexcept {

    seats.diag.set_screenshot_dir(std::string(plat.vfs.root_path()) + "/screenshots");

    if (frame_ != nullptr) {
        frame_main_.emplace(*frame_);
        seats.mains.mains.bind(&*frame_main_);
    }

    if (input_main_)
        seats.mains.mains.bind(&*input_main_);

    if (prefetch_main_)
        seats.mains.mains.bind(&*prefetch_main_);
    if (pcm_main_)
        seats.mains.mains.bind(&*pcm_main_);

    (void)rides_io_(writes_, "IoMain::add_coworker (degraded: no durable writes)");

    (void)rides_io_(discs_, "IoMain::add_coworker (degraded: no disc reads)");

    (void)rides_io_(lw_sector_, "IoMain::add_coworker (degraded: CD sectors stay on SPI)");

    (void)rides_io_(lw_sys_, "IoMain::add_coworker (degraded: the 0x29 status poll stays on SPI)");

    (void)rides_io_(file_streams_, "IoMain::add_coworker (degraded: no file streams)");

    (void)rides_io_(window_jobs_, "IoMain::add_coworker (degraded: no window jobs)");

    (void)rides_io_(disk_relay_, "IoMain::add_coworker (degraded: no disk acts)");

    if (seats.save_states
        && rides_io_(*seats.save_states, "IoMain::add_coworker (degraded: no save states)")) {
        const mister::SeatScope publishes_as_ui{mister::SeatTag::Ui};
        seats.save_states->start();
    }

    (void)rides_io_(storage_, "IoMain::add_coworker (degraded: saves reach no file)");

    if (io_main_)
        seats.mains.mains.bind(&*io_main_);

    if (hal::IBootHandoff* page = plat.programmer.boot_handoff()) {
        owner_.set_boot_cookie(page->handoff()->sdram_cfg);
    }
}

}
