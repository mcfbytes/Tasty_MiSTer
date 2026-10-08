// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_block.h"

#include <cstdio>
#include <string>

#include "cores/mailbox_servants.h"
#include "infra/opt_ref.h"
#include "os/bt_probe.h"
#include "reactor/notifier.h"
#include "report.h"

namespace mister::fw {

using namespace mister;

bool DeviceBlock::rides_io_(svc::IIoCoworker& lane, const char* what) noexcept {
    if (!io_main_) return false;
    if (io_main_->add_coworker(&lane)) return true;
    report(what, Error{Errc::slot_range, ERR_SITE(),
                       static_cast<std::uint32_t>(svc::IoMain::kMaxCoworkers)});
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

    return hal::FpgaMemory::map(regions[static_cast<std::size_t>(hal::RegionId::ScalerOut)],
                                os::MmioRegion::Access::ReadOnly);
}

std::unique_ptr<reactor::FrameClock> DeviceBlock::open_frame_clock(const char* vsync_device,
                                                                   reactor::Executive& exec) {
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
        .diag = session_.diag_cell(),
        .pause_expiries = owner_.pause_expiries_cell(),
        .recover_polls = owner_.recover_polls_cell(),
        .save_write_failures = owner_.save_write_failures_cell(),
        .fallbacks = owner_.fallback_cell(),
        .doorbell = session_.doorbell_cell(),
        .round_timing = plat.round_timer.cell(),

        .frames = frame_ != nullptr ? &frame_->frame_cell() : nullptr,
        .screenshots = shots_,
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
        if (M* m = mains.mains.get<M>(); m != nullptr) grant_pause(Row::seat, m->pause_latch());
    }
}

template <class... Rows>
void DeviceBlock::grant_latched_(const ThreadAssembly::SeatMains& mains,
                                 SeatList<Rows...>) noexcept {
    (grant_if_latched_<Rows>(mains), ...);
}

void DeviceBlock::grant_pauses(const ThreadAssembly::SeatMains& mains, UiMain* ui) noexcept {
    if (ui != nullptr) grant_pause(SeatTag::Ui, ui->pause_latch());
    static_assert(HasPauseLatch<app::InputMain> && HasPauseLatch<app::PcmMain> &&
                      HasPauseLatch<svc::PrefetchMain> && HasPauseLatch<svc::IoMain>,
                  "a renamed pause_latch() would silently drop that seat's grant");
    grant_latched_(mains, SeatMainList{});
}

DeviceBlock::DeviceBlock(const BootParts& plat, app::IStopSignal& stop, const Front& front)
    : front_(front), file_streams_{io_wake_, infra::OptRef<const svc::Vfs>{plat.vfs}}, events_(),
      owner_events_(), clock_(), input_wire_{plat.exec}, video_pump_{plat.vfs,
                                                                     clock_,
                                                                     plat.video,
                                                                     {},
                                                                     hdmi_level_,
                                                                     app::VideoPump::Wiring{
                                                                         .activity = input_wire_,
                                                                         .link_tx = ui_inbox_,
                                                                     }},
      bt_pump_{clock_, &os::bluetooth_adapter_up, infra::OptRef<xthread::DiagLog>{diag_log_}},
      main_rx_{main_wake_}, link_router_{main_rx_, ui_rx_, input_rx_},
      input_emit_{input_wire_, plat.link, link_router_}, input_build_{plat.vfs, input_wire_},
      frame_(open_frame_clock(plat.video.vsync_device, plat.exec)), prefetch_(),
      pcm_feeder_{clock_, pcm_commands_}, mailbox_relay_{pcm_wake_}, companion_host_{pcm_wake_},
      storage_(io_wake_), disc_store_{plat.vfs, disc_geometry_, disc_counters_, prefetch_},
      discs_(io_wake_, {.geometry = disc_geometry_,
                        .counters = disc_counters_,
                        .reader = infra::OptRef<svc::IDiscReader>{disc_store_},
                        .mounter = infra::OptRef<svc::IDiscMounter>{disc_store_}}),
      shots_(), surface_(), osd_wire_{surface_, plat.link},
      ladder_{transitioning_, plat.bridges, infra::OptRef<reactor::RoundTimer>{plat.round_timer}},
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
          .vfs = &plat.vfs,
          .uart_handoffs = front.grants_uart ? &uart_handoffs_ : nullptr,
          .stdout_routing = true,
          .doorbells = plat.doorbells,
          .fpga_mem = plat.fpga_mem,
          .lw_window = plat.lw_window,
          .doorbell_nodes = plat.doorbell_nodes,
          .boot_handoff = plat.programmer.boot_handoff(),

          .storage_channel = &storage_,

          .board_ops = {.reset = &ladder_, .rt = &ladder_, .stop = &stop},
      },
      session_{plat.link, plat.link, plat.link, plat.fpga_mem.region, events_, parts_},
      bitstream_{plat.programmer, plat.bridges, plat.vfs, plat.program},
      owner_{session_.quiesce_channel(),
             app::SessionOwner::Link{
                 .inbox = main_inbox_,
                 .rx = main_rx_,
                 .config_cell = config_cell_,
                 .owner_events = owner_events_,
                 .conf_str_cell = conf_str_cell_,
                 .ladder_cell = ladder_cell_,
                 .main_wake = main_wake_,
                 .ops = parts_.board_ops,
                 .boot_handoff =
                     plat.programmer.boot_handoff() != nullptr
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
                 .pin_levels = pin_levels_}},
      frame_counter_{app::CoreFrameCounter::Wiring{
          .link = &plat.link, .clock = &clock_, .demand = &frame_demand_, .out = &core_frames_}},
      vfs_(&plat.vfs) {}

void DeviceBlock::open_wires(const BootParts& plat) noexcept {
    (void)diag_log_.open(kDiagPath);
    diag_log_.set_echo_stderr(true);
    owner_.set_aperture(plat.fpga_mem.region);
}

void DeviceBlock::open_seats(const BootParts& plat, const Seats& seats) noexcept {

    seats.diag.set_screenshot_dir(std::string(plat.vfs.root_path()) + "/screenshots");

    if (auto w = map_scaler_out(plat.regions); w) {
        frame_src_.emplace(std::move(*w), video_pump_.geometry_cell());
        seats.diag.set_frame_source(&*frame_src_);
    } else {
        report("FpgaMemory::map(scaler-out) (degraded: no screenshots)", w.error());
    }

    if (frame_ != nullptr) {
        frame_main_.emplace(*frame_);
        seats.mains.mains.bind(&*frame_main_);
    }

    if (auto fds = app::InputDecode::Fds::create(input_wire_); !fds) {
        report("InputPipeline::open (degraded: input unavailable)", fds.error());
    } else {
        input_pipeline_.emplace(std::move(*fds), input_wire_, input_build_, input_emit_, clock_,
                                app::InputDecode::Wiring{
                                    .diag = diag_log_,
                                    .inbox = input_inbox_,
                                    .link_rx = input_rx_,
                                    .cell = front_.launcher_state,
                                    .keys = front_.launcher_keys,
                                });
        if (auto park = xthread::ParkFds::create(input_wake_); !park) {
            report("InputMain::open (degraded: input unavailable)", park.error());
        } else if (auto w = input_pipeline_->decode().watch(*park); !w) {
            report("InputMain::open (degraded: input unavailable)", w.error());
        } else {
            input_main_.emplace(input_pipeline_->decode(), std::move(*park),
                                infra::OptRef{main_wake_});

            seats.mains.mains.bind(&*input_main_);
            seats.mains.input_build = &input_build_;
            seats.diag.set_input_sample(&input_pipeline_->sample());
        }
    }

    if (auto po = prefetch_.open(); !po) {
        report("ChdPrefetch::open (degraded: CHD decode stays inline on T-IO)", po.error());
    } else if (auto fds = xthread::ParkFds::create(prefetch_.wake()); !fds) {
        report("PrefetchMain::open (degraded: CHD decode stays inline on T-IO)", fds.error());
    } else {
        prefetch_main_.emplace(prefetch_, std::move(*fds), infra::OptRef{main_wake_});

        session_.attach_prefetch(prefetch_);
        seats.mains.mains.bind(&*prefetch_main_);
    }

    if (auto fds = xthread::ParkFds::create(pcm_wake_); !fds) {
        report("PcmMain::open (degraded: no T-PCM, so no fed window)", fds.error());
    } else {
        pcm_main_.emplace(pcm_feeder_, std::move(*fds), pcm_commands_,
                          app::PcmMain::Mailbox{mailbox_relay_, companion_host_},
                          infra::OptRef{main_wake_});
        seats.mains.mains.bind(&*pcm_main_);

        session_.set_pcm_feeder(&pcm_feeder_);

        if (vfs_ != nullptr) {
            companion_host_.install(cores::make_servants(*vfs_));
            session_.attach_mailbox(mailbox_relay_);
            owner_.set_companion_binds(&companion_host_.binds());
        }
    }

    if (auto fds = xthread::ParkFds::create(io_wake_); !fds) {
        report("IoMain fds (degraded: no T-IO, saves reach no file)", fds.error());
    } else {
        io_main_.emplace(std::move(*fds), infra::OptRef{main_wake_});
    }

    bool writes_ride = false;
    if (vfs_ != nullptr) {
        writes_.emplace(*vfs_, io_wake_);
        writes_ride = rides_io_(*writes_, "IoMain::add_coworker (degraded: no durable writes)");
    }

    const bool discs_ride = rides_io_(discs_, "IoMain::add_coworker (degraded: no disc reads)");

    (void)rides_io_(file_streams_, "IoMain::add_coworker (degraded: no file streams)");

    bool window_jobs_ride = false;
    if (vfs_ != nullptr) {
        window_jobs_.emplace(*vfs_, io_window_map_, plat.fpga_mem.region, io_wake_);
        window_jobs_ride =
            rides_io_(*window_jobs_, "IoMain::add_coworker (degraded: no window jobs)");
    }

    const bool storage_rides =
        rides_io_(storage_, "IoMain::add_coworker (degraded: saves reach no file)");

    if (io_main_) {
        seats.mains.mains.bind(&*io_main_);

        if (writes_ride) session_.set_write_service(&*writes_);
        if (window_jobs_ride) session_.set_window_job_service(&*window_jobs_);

        if (discs_ride) session_.attach_discs(discs_);

        if (storage_rides) session_.set_storage_lifecycle(&storage_);
    }

    {

        const mister::SeatScope asks_as_rt{mister::SeatTag::RT};
        if (hal::IBootHandoff* page = plat.programmer.boot_handoff()) {
            owner_.set_boot_cookie(page->handoff()->sdram_cfg);
        }
    }
}

}  // namespace mister::fw
