// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/hps_framebuffer.h"
#include "app/launcher_screen.h"
#include "app/mgl_pump.h"
#include "app/save_state_attach.h"
#include "boot_parts.h"
#include "device_block.h"
#include "hd_osd_block.h"
#include "host_facts.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "launcher_block.h"
#include "session_mode.h"
#include "stores_block.h"
#include "svc/config_snapshot.h"
#include "thread_assembly.h"
#include "ui/event_trace.h"
#include "ui/menu_model.h"
#include "ui/shell_bt_ops.h"
#include "ui/ui_round.h"
#include "ui/vfs_browse_source.h"
#include "ui_sink.h"

namespace mister::os {
class ProcessTitle;
}

namespace mister::fw {

class MenuBlock final : public SessionMode::Face {
    TASTY_SEAT_EXEMPT(boot);

public:
    MenuBlock(
        const BootParts& plat,
        DeviceBlock& device,
        LauncherBlock& launcher,
        infra::OptRef<os::ProcessTitle> title
    );
    MenuBlock(const MenuBlock&) = delete;
    MenuBlock& operator=(const MenuBlock&) = delete;

    [[nodiscard]] Parts parts() noexcept override;
    void open() noexcept override;
    void route_pages_to(app::ICmdVerbSink& route) noexcept override;
    void latch(const svc::ConfigSnapshot& c, ThreadAssembly::SeatMains& mains) noexcept override;

private:
    const char* diag_path_;
    infra::OptRef<app::BtPump> bt_;
    ui::EventTrace trace_;

    app::UiWriteLane ui_writes_;
    StoresBlock stores_;
    app::MglPump mgl_pump_;
    app::HpsFramebuffer hps_fb_;
    ui::VfsBrowseSource browse_source_;
    ui::ShellBtOps bt_ops_;

    ui::MenuModel ui_model_;
    ui::PageContext ui_context_;
    HostFacts host_facts_;
    UiSink ui_sink_;

    HdOsdBlock hd_;
    ui::UiRound ui_round_;

    app::SaveStateAttach save_states_;
    app::LauncherScreen launcher_screen_;
};

}
