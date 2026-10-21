// SPDX-License-Identifier: GPL-3.0-or-later
#include "hub.h"

#include <memory>
#include <utility>

#include "infra/opt_ref.h"
#include "svc/config.h"
#include "svc/config_snapshot.h"
#include "svc/deadzone_rule.h"

namespace mister::fw {

using namespace mister;

DiagSampler::Sources Hub::diag_sources_(const BootParts& plat) noexcept {
    const DiagSampler::DeviceCells d = device_.diag_sources(plat);
    const SessionMode::Face::Parts& f = face_parts_;
    return DiagSampler::Sources{
        .mgl_row0 = d.mgl_row0,
        .window_counts = d.window_counts,
        .bay_counts = d.bay_counts,
        .video_stats = device_.video_pump_.stats_cell(),
        .video_geometry = device_.video_pump_.geometry_cell(),
        .video_wire = device_.video_pump_.wire(),
        .diag = d.diag,
        .pause_expiries = d.pause_expiries,
        .recover_polls = d.recover_polls,
        .save_write_failures = d.save_write_failures,
        .fallbacks = d.fallbacks,
        .ini_write_failures = d.ini_write_failures,
        .doorbell = d.doorbell,
        .round_timing = d.round_timing,
        .frames = d.frames,
        .ui_pages = f.ui_pages,
        .hd = f.hd,
        .replay = replay_.replay_status_,
        .rec_capture = rec_.rec_status_,
        .rec_encode = rec_.encode_status_,
        .rec_write = rec_.rec_write_status_,
        .rec_avi = rec_.avi_write_status_,
        .screenshots = d.screenshots,
    };
}

Hub::Hub(const BootParts& plat, app::IStopSignal& stop, SessionMode& mode)
    : front_{mode.front(plat)},
      device_{plat, stop, front_->device_front()},
      face_{front_->face(plat, device_)},
      face_parts_{face_->parts()},
      shot_pump_{
          app::ScreenshotPump::Wiring{.queue = device_.shots_, .sink = face_parts_.shot_sink}
      },
      replay_{plat, device_, shot_pump_, face_parts_.osd_close},
      rec_{plat, device_, replay_.replay_status_},
      tail_{mode.tail(plat, device_, replay_, rec_)},
      router_{app::CmdRouter::Wiring{
          .mgl = face_parts_.mgl,
          .video = device_.video_pump_,
          .fb = face_parts_.fb,
          .shots = shot_pump_,
          .replay = replay_.replay_feeder_,
          .rec = rec_.recorder_,
          .ui_requests = device_.owner_.ui_requests(),
          .stats = plat.stats,
          .link_tx = device_.ui_inbox_
      }},
      fifo_(DeviceBlock::open_cmd_fifo(router_)),
      rt_main_{
          plat.exec,
          plat.link_timing,
          app::RtMain::Wiring{
              .session = infra::OptRef<app::LinkSession>{device_.session_},
              .input = infra::OptRef<app::InputEmit>{device_.input_emit_},
              .osd = infra::OptRef<app::OsdWire>{device_.osd_wire_},
              .timer = infra::OptRef<reactor::RoundTimer>{plat.round_timer},
              .wire = infra::OptRef<app::InputWire>{device_.input_wire_},
              .frames = infra::OptRef<app::CoreFrameCounter>{device_.frame_counter_},
              .scanout = front_->scanout(),
              .replay = infra::OptRef<app::ReplayGate>{replay_.replay_gate_},
              .replay_ring = wired_end(replay_.replay_ring_.receiver(), "ReplayRing receiver"),
          }
      },
      assembly_{
          plat.threads,
          plat.stats,
          device_.events_,
          plat.rt_evidence,
          device_.main_wake_,
          ThreadAssembly::DiagWires{
              .log = device_.diag_log_,
              .rt_lane = plat.rt_lane,
              .transitioning = device_.transitioning_
          },
          UiMain::Wires{.wake = device_.ui_wake_, .uart_handoffs = device_.uart_handoffs_},

          UiMain::Wiring{
              .fifo = fifo_ ? infra::OptRef<app::CmdFifo>{*fifo_} : infra::OptRef<app::CmdFifo>{},
              .mgl = face_parts_.mgl,
              .video = device_.video_pump_,
              .uart = face_parts_.uart,
              .ui_sink = face_parts_.ui_sink,
              .owner_events = device_.owner_events_,
              .bt = face_parts_.bt,
              .shots = shot_pump_,
              .replay = replay_.replay_feeder_,
              .recorder = rec_.recorder_,
              .launcher_screen = face_parts_.launcher_screen,
              .ui_round = face_parts_.ui_round,
              .link_rx = device_.ui_rx_,
              .save_states = face_parts_.save_states,
              .owner_tick = tail_->owner_tick()
          },
          diag_sources_(plat),
          device_.diag_services()
      } {

    face_->open();
    device_.open_wires();

    face_->route_pages_to(router_);

    device_.open_seats(
        plat,
        DeviceBlock::Seats{
            .mains = seat_mains_,
            .diag = assembly_.diag(),
            .save_states = face_parts_.save_states
        }
    );

    rec_.open_seats(seat_mains_);

    device_.grant_pauses(seat_mains_, assembly_.ui());
}

Hub::~Hub() {
    assembly_.stop_join_or_exit();
}

Ex<void> Hub::open_process_main() noexcept {
    if (process_main_)
        return {};
    auto fds = xthread::ParkFds::create(device_.main_wake_, device_.main_stop_);
    if (!fds)
        return std::unexpected(fds.error());
    process_main_.emplace(device_.owner_, assembly_, std::move(*fds));
    return {};
}

void Hub::latch_boot_config() noexcept {

    auto c = std::make_unique<svc::ConfigSnapshot>();
    if (device_.config_cell_.sample_into(*c) == 0)
        c = std::make_unique<svc::ConfigSnapshot>();

    const auto deadzones = svc::parse_deadzone_rules(*c);
    device_.input_build_.set_cfg_deadzone_rules(deadzones);
    if (device_.input_pipeline_)
        device_.input_pipeline_->latch_cfg(deadzones, c->rumble != 0);

    front_->latch(device_, seat_mains_);
    face_->latch(*c, seat_mains_);
}

}
