// SPDX-License-Identifier: GPL-3.0-or-later
#include "hub.h"

#include <cstdio>

#include "tasty_ctl.h"
#include "infra/persist.h"
#include "svc/config.h"
#include "svc/config_snapshot.h"
#include "svc/deadzone_rule.h"
#include "os/bt_probe.h"
#include "reactor/core_state.h"
#include "hal/axi.h"
#include "reactor/notifier.h"

namespace mister::fw {

using namespace mister;

void report(const char* what, const Error& e) {
    char buf[192];
    std::snprintf(buf, sizeof buf, "mister: %s failed: Errc=%u site=%u detail=%u", what,
                  static_cast<unsigned>(e.code), static_cast<unsigned>(e.site),
                  static_cast<unsigned>(e.detail));
    tasty_say(buf);
}

bool Hub::rides_io_(svc::IIoCoworker& lane, const char* what) noexcept {
    if (io_main_.add_coworker(&lane)) return true;
    report(what, Error{Errc::slot_range, ERR_SITE(),
                       static_cast<std::uint32_t>(svc::IoMain::kMaxCoworkers)});
    return false;
}

std::optional<app::CmdFifo> Hub::open_cmd_fifo() {
    std::optional<app::CmdFifo> fifo;
    if (auto f = app::CmdFifo::open(); f) {
        fifo.emplace(std::move(*f));
    } else {
        report("CmdFifo::open (degraded: no /dev/MiSTer_cmd)", f.error());
    }
    return fifo;
}

Ex<hal::FpgaMemory> Hub::map_scaler_out(std::span<const hal::PhysRegion> regions) {

    if (regions.size() != hal::kRegionCount) {
        return std::unexpected(Error{Errc::dt_missing, ERR_SITE(), 0});
    }

    return hal::FpgaMemory::map(regions[static_cast<std::size_t>(hal::RegionId::ScalerOut)],
                                os::MmioRegion::Access::ReadOnly);
}

std::optional<hal::ScalerBuffers> Hub::map_scaler_buffers(
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

std::unique_ptr<reactor::FrameClock> Hub::open_frame_clock(const char* vsync_device,
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

void Hub::grant_pause_(hal::Seat seat, xthread::PauseLatch& latch) noexcept {
    latch.bind_asker_wake(main_wake_);
    owner_.grant_pause(seat, latch);
}

Hub::Hub(const BootParts& plat, app::IStopSignal& stop)
    : events_(), owner_events_(), clock_(), video_pump_{plat.vfs, clock_, plat.video},
      bt_pump_{clock_, &os::bluetooth_adapter_up}, main_rx_{main_wake_},
      link_router_{main_rx_, ui_rx_, input_rx_},
      input_pipeline_{plat.vfs, plat.link, clock_, link_router_},
      input_main_{input_pipeline_.decode()}, input_sample_(input_pipeline_.sample()),
      router_{nullptr, &video_pump_}, fifo_(open_cmd_fifo()),
      frame_(open_frame_clock(plat.video.vsync_device, plat.exec)), prefetch_(),
      prefetch_main_{prefetch_}, pcm_feeder_{clock_}, pcm_main_{pcm_feeder_}, io_main_{},
      storage_(io_main_.wake()), discs_(io_main_.wake()), shots_(), shot_pump_(), surface_(),
      osd_wire_{surface_, plat.link},
      parts_{
          .video = video_pump_.wire(),
          .input = input_pipeline_.wire(),
          .osd = osd_wire_,
          .link_inbox = main_inbox_,
          .link_rx = main_rx_,
          .config_cell = config_cell_,
          .ui_inbox = ui_inbox_,
          .input_inbox = input_inbox_,
          .router = link_router_,
          .vfs = &plat.vfs,
          .io_wake = &io_main_.wake(),
          .main_wake = &main_wake_,
          .stdout_routing = true,
          .doorbells = plat.doorbells,
          .fpga_mem = plat.fpga_mem,
          .lw_window = plat.lw_window,
          .doorbell_nodes = plat.doorbell_nodes,
          .exec = &plat.exec,
          .core_state = &core_state_,
          .clock = &clock_,
      },
      session_{plat.link, plat.link, plat.link, plat.fpga_mem.region, events_, parts_},
      hdmi_level_{session_.pin_level_cell()},
      assembly_{
          plat.threads,
          plat.stats,
          events_,
          plat.rt_evidence,
          main_wake_,
          UiMain::Wiring{.fifo = fifo_ ? &*fifo_ : nullptr, .mgl = nullptr, .video = &video_pump_}},
      bitstream_{plat.programmer, plat.bridges, plat.vfs, plat.program},
      owner_{session_.quiesce_channel(),
             app::SessionOwner::Link{main_inbox_, main_rx_, config_cell_, owner_events_,
                                     conf_str_cell_, &main_wake_}},
      process_main_{owner_, assembly_, main_wake_}, null_osd_{},
      tasty_sink_{video_pump_, &session_.identity()},
      frame_counter_{app::CoreFrameCounter::Wiring{
          .link = &plat.link, .clock = &clock_, .demand = &frame_demand_, .out = &core_frames_}},
      replay_gate_{
          app::ReplayGate::Wiring{.link = &plat.link,
                                  .frames = &frame_counter_,
                                  .emitter = &input_pipeline_.emit().emitter(),
                                  .clock = &clock_,
                                  .control = &replay_control_,
                                  .status = &replay_status_,
                                  .vsync = frame_ != nullptr ? &frame_->frame_cell() : nullptr}},
      replay_feeder_{app::ReplayFeeder::Wiring{.vfs = &plat.vfs,
                                               .clock = &clock_,
                                               .ring = &replay_ring_,
                                               .control = &replay_control_,
                                               .status = &replay_status_,
                                               .identity = &session_.identity(),
                                               .core_status = &session_.status_cell(),
                                               .conf = &conf_str_cell_,
                                               .asks = &owner_.ui_requests(),
                                               .video = &video_pump_,
                                               .input = &input_pipeline_.wire(),
                                               .shots = &shot_pump_,
                                               .diag = &assembly_.diag().diag_sink(),
                                               .osd = &null_osd_,
                                               .settings_tx = &ui_inbox_}},
      copier_{app::FrameCopier::Wiring{.clock = &clock_,
                                       .delay = &capture_delay_,
                                       .control = &rec_control_,
                                       .status = &rec_status_,
                                       .demand = &frame_demand_,
                                       .frames = &core_frames_,
                                       .replay = &replay_status_,
                                       .writer = &rec_write_status_,
                                       .avi_writer = &avi_write_status_,
                                       .video = &video_pump_.geometry_cell(),
                                       .channel = &raw_frames_},
              map_scaler_buffers(plat.regions)},
      avi_encoder_{app::AviEncoder::Wiring{
          .out = &chunks_, .raw = &raw_frames_, .clock = &clock_, .cpu = &encode_cpu_}},
      hasher_{app::FrameHasher::Wiring{.channel = &raw_frames_,
                                       .out = &sidecar_ring_,
                                       .writer_wake = &rec_write_wake_,
                                       .status = &encode_status_,
                                       .clock = &clock_,
                                       .video = &avi_encoder_}},
      sidecar_writer_{app::SidecarWriter::Wiring{
          .in = &sidecar_ring_, .status = &rec_write_status_, .clock = &clock_}},
      avi_writer_{
          app::AviWriter::Wiring{.in = &chunks_, .status = &avi_write_status_, .clock = &clock_}},
      capture_main_{copier_, capture_wake_}, encode_main_{hasher_, encode_wake_, encode_delay_},
      rec_write_main_{sidecar_writer_, avi_writer_, rec_write_wake_},
      recorder_{app::RecorderControl::Wiring{.control = &rec_control_,
                                             .capture_wake = &capture_wake_,
                                             .status = &rec_status_,
                                             .writer = &rec_write_status_,
                                             .diag = &assembly_.diag().diag_sink(),
                                             .identity = &session_.identity()}},
      ladder_{assembly_, plat.bridges},
      rt_main_{plat.exec,  &session_,     &input_pipeline_.emit(), plat.link_timing,
               &osd_wire_, &round_timer_, &input_pipeline_.wire()},
      vfs_(&plat.vfs), sd_block_(plat.kernel.sd_block) {
    owner_.set_aperture(plat.fpga_mem.region);
    input_pipeline_.wire().set_rt_wake(&plat.exec);
    if (fifo_) router_.install(*fifo_);

    router_.bind_rt_stats(&plat.stats);

    router_.bind_link_tx(&ui_inbox_);

    shot_pump_.bind_queue(&shots_);
    router_.bind_screenshots(&shot_pump_);
    assembly_.ui().set_screenshot_pump(&shot_pump_);
    assembly_.diag().set_screenshot_queue(&shots_);
    assembly_.diag().set_screenshot_dir(std::string(plat.vfs.root_path()) + "/screenshots");

    if (auto w = map_scaler_out(plat.regions); w) {
        frame_src_.emplace(std::move(*w), video_pump_.geometry_cell());
        assembly_.diag().set_frame_source(&*frame_src_);
    } else {
        report("FpgaMemory::map(scaler-out) (degraded: no screenshots)", w.error());
    }

    video_pump_.wire().attach_link_tx(ui_inbox_);
    assembly_.ui().set_ui_sink(&tasty_sink_);
    assembly_.ui().set_owner_events(&owner_events_);
    (void)assembly_.diag().open_diag(kDiagPath);
    assembly_.diag().set_diag_echo_stderr(true);
    assembly_.diag().set_diag_cell(&session_.diag_cell());
    assembly_.diag().set_mgl_row0_cell(&owner_.mgl_row0_cell());
    assembly_.diag().set_window_counts_cell(&owner_.window_cell());
    assembly_.diag().set_pause_expiries_cell(&owner_.pause_expiries_cell());
    assembly_.diag().set_recover_polls_cell(&owner_.recover_polls_cell());
    assembly_.diag().set_save_write_failures_cell(&owner_.save_write_failures_cell());
    assembly_.diag().set_fallback_cell(&owner_.fallback_cell());
    assembly_.diag().set_doorbell_cell(&session_.doorbell_cell());
    assembly_.diag().set_round_timing_cell(&round_timer_.cell());

    video_pump_.set_hdmi_int_source(hdmi_level_);

    video_pump_.set_activity_source(input_pipeline_.wire());
    bt_pump_.set_diag(&assembly_.diag().diag_sink());
    assembly_.ui().set_link_rx(&ui_rx_);

    if (frame_ != nullptr) {
        assembly_.diag().set_frame_cell(&frame_->frame_cell());
        frame_main_.emplace(*frame_);
        seat_mains_.frame = &*frame_main_;
    }

    input_pipeline_.decode().set_diag(&assembly_.diag().diag_sink());

    session_.attach_input_emitter(input_pipeline_.emit().emitter());
    input_pipeline_.decode().set_link_inbox(&input_inbox_);
    input_pipeline_.decode().set_link_rx(&input_rx_);
    if (auto is = input_pipeline_.open(); !is) {

        report("InputPipeline::open (degraded: input unavailable)", is.error());
    } else if (auto im = input_main_.open(); !im) {
        report("InputMain::open (degraded: input unavailable)", im.error());
    } else {

        seat_mains_.input = &input_main_;
        seat_mains_.input_build = &input_pipeline_.build();
        assembly_.diag().set_input_sample(&input_sample_);
    }

    if (auto po = prefetch_.open(); !po) {
        report("ChdPrefetch::open (degraded: CHD decode stays inline on T-IO)", po.error());
    } else if (auto pm = prefetch_main_.open(); !pm) {
        report("PrefetchMain::open (degraded: CHD decode stays inline on T-IO)", pm.error());
    } else {

        session_.attach_prefetch(prefetch_);
        seat_mains_.prefetch = &prefetch_main_;
    }

    if (auto fo = pcm_main_.open(); !fo) {
        report("PcmMain::open (degraded: no T-PCM, so no fed window)", fo.error());
    } else {
        seat_mains_.pcm = &pcm_main_;

        session_.set_pcm_feeder(&pcm_feeder_);
    }

    session_.block_slots().attach_channel(storage_);

    bool writes_ride = false;
    if (vfs_ != nullptr) {
        writes_.emplace(*vfs_, io_main_.wake());
        writes_ride = rides_io_(*writes_, "IoMain::add_coworker (degraded: no durable writes)");
    }

    bool discs_ride = false;
    if (vfs_ != nullptr) {
        disc_store_.emplace(*vfs_, discs_.geometry_cell(), discs_.counters_cell());
        disc_store_->bind_prefetch(&prefetch_);
        discs_.bind_reader(*disc_store_);
        discs_.bind_mounter(*disc_store_);
        discs_ride = rides_io_(discs_, "IoMain::add_coworker (degraded: no disc reads)");
    }

    (void)rides_io_(session_.stream_coworker(), "IoMain::add_coworker (degraded: no file streams)");

    bool window_jobs_ride = false;
    if (vfs_ != nullptr) {
        window_jobs_.emplace(*vfs_, io_window_map_, plat.fpga_mem.region, io_main_.wake());
        window_jobs_ride =
            rides_io_(*window_jobs_, "IoMain::add_coworker (degraded: no window jobs)");
    }

    const bool storage_rides =
        rides_io_(storage_, "IoMain::add_coworker (degraded: saves reach no file)");

    if (auto io = io_main_.open(); !io) {
        report("IoMain::open (degraded: no T-IO, saves reach no file)", io.error());
    } else {
        seat_mains_.io = &io_main_;

        if (writes_ride) session_.set_write_service(&*writes_);
        if (window_jobs_ride) session_.set_window_job_service(&*window_jobs_);

        if (discs_ride) session_.attach_discs(discs_);

        if (storage_rides) session_.set_storage_lifecycle(&storage_);
    }

    {
        app::BoardOps ops{};
        ops.rt = &ladder_;
        ops.stop = &stop;
        ops.reset = &ladder_;

        session_.set_board_ops(ops);
        owner_.set_board_ops(ops);

        const mister::SeatScope asks_as_rt{mister::SeatTag::RT};
        owner_.set_boot_handoff(plat.programmer.boot_handoff());
        session_.set_boot_handoff(plat.programmer.boot_handoff());

        owner_.set_programmer(&bitstream_);
        owner_.set_storage(&plat.vfs);

        owner_.set_fabric_cell(&session_.fabric_cell());

        owner_.set_status_cell(&session_.status_cell());

        owner_.set_pin_levels(&session_.pin_level_cell());

        owner_.set_mount_status_cell(&session_.mount_status_cell());

        owner_.set_window_map(&window_map_);
        owner_.set_file_tx_level_cell(&session_.file_tx_level_cell());
        owner_.set_save_extent_cell(&session_.save_extent_cell());
        session_.set_ladder_cell(&owner_.ladder_cell());
        if (hal::IBootHandoff* page = plat.programmer.boot_handoff()) {
            owner_.set_boot_cookie(page->handoff()->sdram_cfg);
        }
    }
    router_.bind_ui_requests(&owner_.ui_requests());

    router_.bind_replay(&replay_feeder_);
    assembly_.ui().set_replay(&replay_feeder_);
    rt_main_.bind_replay(&replay_gate_, &replay_ring_);
    assembly_.diag().set_replay_cell(&replay_status_);

    rt_main_.bind_frames(&frame_counter_);
    router_.bind_recorder(&recorder_);
    assembly_.ui().set_recorder(&recorder_);
    assembly_.diag().set_recorder_cells(&rec_status_, &encode_status_, &rec_write_status_,
                                        &avi_write_status_);
    if (auto c = capture_main_.open(); !c) {
        report("CaptureMain::open (degraded: no recorder)", c.error());
    } else if (auto e = encode_main_.open(); !e) {
        report("EncodeMain::open (degraded: no recorder)", e.error());
    } else if (auto w = rec_write_main_.open(); !w) {
        report("RecWriteMain::open (degraded: no recorder)", w.error());
    } else {
        seat_mains_.capture = &capture_main_;
        seat_mains_.encode = &encode_main_;
        seat_mains_.rec_write = &rec_write_main_;
    }

    grant_pause_(hal::Seat::Ui, assembly_.ui().pause_latch());
    if (seat_mains_.input != nullptr) grant_pause_(hal::Seat::Input, input_main_.pause_latch());
    if (seat_mains_.prefetch != nullptr)
        grant_pause_(hal::Seat::Prefetch, prefetch_main_.pause_latch());
    if (seat_mains_.pcm != nullptr) grant_pause_(hal::Seat::Pcm, pcm_main_.pause_latch());
    if (seat_mains_.io != nullptr) grant_pause_(hal::Seat::Io, io_main_.pause_latch());

    plat.exec.set_round_timer(&round_timer_);
    ladder_.set_round_timer(&round_timer_);

    plat.exec.set_log_lane(assembly_.diag().rt_log_lane());
    session_.set_log_lane(assembly_.diag().rt_log_lane());
}

Hub::~Hub() {
    if (!assembly_.any_live()) return;
    assembly_.stop();
    (void)assembly_.join();
}

void Hub::latch_boot_config() noexcept {

    auto c = std::make_unique<svc::ConfigSnapshot>();
    if (config_cell_.sample_into(*c) == 0) c = std::make_unique<svc::ConfigSnapshot>();

    const auto deadzones = svc::parse_deadzone_rules(*c);
    input_pipeline_.build().set_cfg_deadzone_rules(deadzones);
}

}  // namespace mister::fw
