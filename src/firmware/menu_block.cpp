// SPDX-License-Identifier: GPL-3.0-or-later
#include "menu_block.h"

#include "app/hd_look_ini.h"
#include "hal/axi.h"
#include "infra/opt_ref.h"
#include "os/process_title.h"

namespace mister::fw {

using namespace mister;

static_assert(
    UiSink::kSeat == UiMain::kSeat,
    "item: UiSink is installed into UiMain (assembly_.ui()) and "
    "runs on whatever seat that main takes."
);
static_assert(
    HostFacts::kSeat == UiMain::kSeat,
    "item: the host sampler runs inside ui::UiRound::round, which "
    "is UiMain's last tick seat."
);

MenuBlock::MenuBlock(
    const BootParts& plat,
    const DeviceBlock::Parts& parts,
    OpenVerdict& verdict,
    LauncherBlock& launcher,
    infra::OptRef<os::ProcessTitle> title
)
    : diag_path_{plat.diag_path},
      bt_{parts.wires.bt},
      trace_{parts.wires.events, parts.wires.owner_events},
      ui_writes_{plat.vfs, parts.wires.io_wake},
      stores_{plat, parts.cells.cheat_blob, ui_writes_},
      mgl_pump_{
          plat.vfs,
          parts.wires.clock,
          app::MglPump::Wiring{
              .link_tx = parts.wires.ui_inbox,
              .asks = parts.session.owner.ui_requests(),
              .identity = parts.session.link.identity(),
              .conf = parts.cells.conf_str,
              .names = stores_.names_,
              .diag = parts.wires.diag
          }
      },
      hps_fb_{
          parts.wires.video.wire(),
          parts.wires.video,
          hal::declared_region(plat.regions, hal::RegionId::VideoFb).value_or(hal::PhysRegion{})
      },
      browse_source_{plat.vfs},

      ui_model_{ui::MenuModel::Wiring{
          .conf_str = parts.cells.conf_str,
          .status = parts.session.link.status_cell(),
          .dip_labels = parts.session.link.dip_labels_cell(),
          .dip_choices = parts.session.link.dip_choices_cell(),
          .option = parts.session.link.option_cell(),
          .launcher = launcher.launcher_state_,
          .config_slot = parts.session.owner.config_slot_cell(),
          .tx_digest = parts.cells.cheat_link.tx_digest_cell(),
          .cheat_catalog = parts.cells.cheat_catalog,
          .cheats = stores_.cheats_,
          .identity = parts.session.link.identity(),
      }},

      ui_context_{
          .recents = infra::OptRef<app::RecentsStore>{stores_.recents_},
          .names = infra::OptRef<app::NameConfig>{stores_.names_},
          .played = infra::OptRef<app::PlayedHistory>{stores_.played_},
          .cheats = infra::OptRef<app::CheatStore>{stores_.cheats_},
          .ui_requests = infra::OptRef<app::UiRequestRing>{parts.session.owner.ui_requests()},
          .bt = infra::OptRef<ui::IBtOps>{bt_ops_},
          .files = infra::OptRef<const svc::Vfs>{plat.vfs},
          .identity = infra::OptRef<const app::IdentityLatch>{parts.session.link.identity()}
      },
      host_facts_{plat.vfs, parts.wires.clock, ui_model_, plat.kernel_rt},
      ui_sink_{UiSink::Wiring{
          .menu = ui_model_,
          .ctx = ui_context_,
          .trace = trace_,
          .mgl = mgl_pump_,
          .video = parts.wires.video,
          .input = parts.wires.input,
          .identity = parts.session.link.identity(),
          .conf_str = parts.cells.conf_str,
          .title = title
      }},
      hd_{plat, parts.wires.clock, hps_fb_, parts.wires.video, verdict},
      ui_round_{
          parts.wires.osd,
          parts.wires.input,
          parts.wires.clock,
          ui_model_,
          ui_context_,
          ui::UiRound::Wiring{
              .browse = browse_source_,
              .link_tx = parts.wires.ui_inbox,
              .sampler = host_facts_,
              .osd_file = ui_sink_,
              .conf_str = parts.cells.conf_str,
              .hd = hd_.screen(),
              .writes = infra::OptRef<app::UiWriteLane>{ui_writes_},
              .video = infra::OptRef<const app::VideoPump>{parts.wires.video},
              .fb = infra::OptRef<const app::HpsFramebuffer>{hps_fb_},
          }
      },
      save_states_{app::SaveStateAttach::Wiring{
          .asks = parts.cells.states_asks,
          .facts = parts.cells.states_facts,
          .copy = parts.cells.save_copy,
          .vfs = plat.vfs,
          .map = parts.cells.io_windows,
          .ui_map = parts.cells.ui_windows,
          .aperture = plat.fpga_mem.region,
          .clock = parts.wires.clock,
          .io_wake = parts.wires.io_wake,
          .toast = infra::OptRef<app::IEventSink>{ui_sink_}
      }},
      launcher_screen_{app::LauncherScreen::Wiring{
          .state = launcher.launcher_state_,
          .fb_ack = launcher.launcher_fb_ack_,
          .launcher_wake = launcher.launcher_wake_,
          .fb = hps_fb_,
          .video = parts.wires.video,
          .osd = ui_model_
      }} {

    mgl_pump_.set_info(infra::OptRef<app::InfoSink>{info_sink_});
    ui_context_.info = infra::OptRef<app::InfoSink>{info_sink_};
}

SessionMode::Face::Parts MenuBlock::parts() noexcept {
    return Parts{
        .osd_close = ui_model_,
        .ui_sink = ui_sink_,
        .shot_sink = infra::OptRef<app::IEventSink>{ui_sink_},
        .mgl = infra::OptRef<app::MglPump>{mgl_pump_},
        .fb = infra::OptRef<app::HpsFramebuffer>{hps_fb_},
        .bt = bt_,
        .launcher_screen = infra::OptRef{launcher_screen_},
        .ui_round = infra::OptRef<app::IUiRoundPort>{ui_round_},
        .save_states = infra::OptRef<app::SaveStateAttach>{save_states_},
        .ui_writes = infra::OptRef<app::UiWriteLane>{ui_writes_},
        .verdicts =
            app::VerdictSinks{
                .played = infra::OptRef<app::IWriteVerdict>{stores_.played_},
                .recents = infra::OptRef<app::IWriteVerdict>{stores_.recents_}
            },
        .ui_pages = infra::OptRef{ui_round_.page_cell()},
        .hd = infra::OptRef<const app::HdOsdStatusCell>{hd_.hd_status_},
    };
}

void MenuBlock::open() noexcept {
    (void)trace_.open(diag_path_);
}

void MenuBlock::route_pages_to(app::ICmdVerbSink& route) noexcept {
    ui_context_.route = infra::OptRef<app::ICmdVerbSink>{route};
}

void MenuBlock::latch(const svc::ConfigSnapshot& c, ThreadAssembly::SeatMains& mains) noexcept {
    ui_context_.bt_reset_before_pair = c.bt_reset_before_pair != 0;
    ui_sink_.set_log_file_entry(c.log_file_entry != 0);
    stores_.latch(c);

    ui_model_.set_osd_style(c.osd_style == 1);
    ui_model_.set_hd_look(app::hd_look_from_ini(c));
    (void)hd_.open_seat(c, mains);
}

}
