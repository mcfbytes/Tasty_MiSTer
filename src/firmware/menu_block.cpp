// SPDX-License-Identifier: GPL-3.0-or-later
#include "menu_block.h"

#include "app/hd_look_ini.h"
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

namespace {

hal::PhysRegion video_fb_region(std::span<const hal::PhysRegion> regions) noexcept {
    if (regions.size() != hal::kRegionCount)
        return hal::PhysRegion{};
    return regions[static_cast<std::size_t>(hal::RegionId::VideoFb)];
}
}

MenuBlock::MenuBlock(
    const BootParts& plat,
    DeviceBlock& device,
    LauncherBlock& launcher,
    infra::OptRef<os::ProcessTitle> title
)
    : diag_path_{plat.diag_path},
      bt_{device.bt_pump_},
      trace_{device.events_},
      ui_writes_{plat.vfs, device.io_wake_},
      stores_{plat, device.cheat_blob_, ui_writes_},
      mgl_pump_{
          plat.vfs,
          device.clock_,
          app::MglPump::Wiring{
              .link_tx = device.ui_inbox_,
              .asks = device.owner_.ui_requests(),
              .identity = device.session_.identity(),
              .conf = device.conf_str_cell_,
              .names = stores_.names_,
              .diag = device.diag_log_
          }
      },
      hps_fb_{device.video_pump_.wire(), device.video_pump_, video_fb_region(plat.regions)},
      browse_source_{plat.vfs},

      ui_model_{ui::MenuModel::Wiring{
          .conf_str = device.conf_str_cell_,
          .status = device.session_.status_cell(),
          .dip = device.session_.dip_cell(),
          .option = device.session_.option_cell(),
          .launcher = launcher.launcher_state_,
          .config_slot = device.session_.config_slot_cell(),
          .tx_digest = device.cheat_link_.tx_digest_cell(),
          .cheat_catalog = device.cheat_catalog_,
          .cheats = stores_.cheats_,
          .identity = device.session_.identity(),
      }},

      ui_context_{
          .recents = infra::OptRef<app::RecentsStore>{stores_.recents_},
          .names = infra::OptRef<app::NameConfig>{stores_.names_},
          .played = infra::OptRef<app::PlayedHistory>{stores_.played_},
          .cheats = infra::OptRef<app::CheatStore>{stores_.cheats_},
          .ui_requests = infra::OptRef<app::UiRequestRing>{device.owner_.ui_requests()},
          .bt = infra::OptRef<ui::IBtOps>{bt_ops_},
          .files = infra::OptRef<const svc::Vfs>{plat.vfs},
          .identity = infra::OptRef<const app::IdentityLatch>{device.session_.identity()}
      },
      host_facts_{plat.vfs, device.clock_, ui_model_, plat.kernel_rt},
      ui_sink_{UiSink::Wiring{
          .menu = infra::OptRef{ui_model_},
          .ctx = ui_context_,
          .trace = trace_,
          .mgl = mgl_pump_,
          .video = device.video_pump_,
          .input = device.input_wire_,
          .identity = device.session_.identity(),
          .conf_str = device.conf_str_cell_,
          .title = title
      }},
      hd_{plat, device.clock_, hps_fb_, device.video_pump_, device.verdict()},
      ui_round_{
          device.osd_wire_,
          device.input_wire_,
          device.clock_,
          ui_model_,
          ui_context_,
          ui::UiRound::Wiring{
              .browse = browse_source_,
              .link_tx = device.ui_inbox_,
              .sampler = host_facts_,
              .osd_file = ui_sink_,
              .conf_str = device.conf_str_cell_,
              .hd = hd_.screen(),
              .writes = infra::OptRef<app::UiWriteLane>{ui_writes_},
          }
      },
      save_states_{app::SaveStateAttach::Wiring{
          .asks = device.save_state_asks_,
          .facts = device.save_state_facts_,
          .copy = device.save_copy_,
          .vfs = plat.vfs,
          .map = device.io_window_map_,
          .ui_map = device.ui_window_map_,
          .aperture = plat.fpga_mem.region,
          .clock = device.clock_,
          .io_wake = device.io_wake_,
          .toast = infra::OptRef<app::IEventSink>{ui_sink_}
      }},
      launcher_screen_{app::LauncherScreen::Wiring{
          .state = launcher.launcher_state_,
          .fb_ack = launcher.launcher_fb_ack_,
          .launcher_wake = launcher.launcher_wake_,
          .fb = hps_fb_,
          .video = device.video_pump_,
          .osd = ui_model_
      }} {}

SessionMode::Face::Parts MenuBlock::parts() noexcept {
    return Parts{
        .osd_close = ui_model_,
        .ui_sink = ui_sink_,
        .shot_sink = infra::OptRef<app::IEventSink>{ui_sink_},
        .mgl = infra::OptRef<app::MglPump>{mgl_pump_},
        .fb = infra::OptRef<app::HpsFramebuffer>{hps_fb_},
        .uart = true,
        .bt = bt_,
        .launcher_screen = infra::OptRef{launcher_screen_},
        .ui_round = infra::OptRef<app::IUiRoundPort>{ui_round_},
        .save_states = infra::OptRef<app::SaveStateAttach>{save_states_},
        .ui_writes = infra::OptRef<app::UiWriteLane>{ui_writes_},
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
