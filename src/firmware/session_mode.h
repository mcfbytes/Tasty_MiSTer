// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "app/cmd_verb_sink.h"
#include "app/event_sink.h"
#include "app/hd_osd_status.h"
#include "app/osd_close.h"
#include "app/owner_tick.h"
#include "app/ui_page_record.h"
#include "device_block.h"
#include "hal/board_profile.h"
#include "hal/board_windows.h"
#include "hal/compatible_blob.h"
#include "hal/irq_pin.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "os/kernel_rt.h"
#include "svc/config_snapshot.h"
#include "svc/vfs.h"
#include "thread_assembly.h"

namespace mister::app {
class BtPump;
class HpsFramebuffer;
class LauncherScreen;
class LinkSession;
class MglPump;
class SaveStateAttach;
class ScanoutRelay;
class SessionOwner;
class IUiRoundPort;
}

namespace mister::fw {

struct BootParts;
class RecorderBlock;
class ReplayBlock;

class SessionMode {
    TASTY_SEAT_EXEMPT(boot);

public:
    using Exit = std::optional<int>;

    class Face {
        TASTY_SEAT_EXEMPT(boot);

    public:
        struct Parts {
            app::IOsdClose& osd_close;
            app::IEventSink& ui_sink;
            infra::OptRef<app::IEventSink> shot_sink{};
            infra::OptRef<app::MglPump> mgl{};
            infra::OptRef<app::HpsFramebuffer> fb{};
            bool uart = false;
            infra::OptRef<app::BtPump> bt{};
            infra::OptRef<app::LauncherScreen> launcher_screen{};
            infra::OptRef<app::IUiRoundPort> ui_round{};
            infra::OptRef<app::SaveStateAttach> save_states{};
            infra::OptRef<const app::UiPageCell> ui_pages{};
            infra::OptRef<const app::HdOsdStatusCell> hd{};
        };

        virtual ~Face() = default;
        [[nodiscard]] virtual Parts parts() noexcept = 0;

        virtual void open() noexcept = 0;

        virtual void route_pages_to(app::ICmdVerbSink& route) noexcept = 0;

        virtual void latch(
            const svc::ConfigSnapshot& c,
            ThreadAssembly::SeatMains& mains
        ) noexcept = 0;
    };

    class Front {
        TASTY_SEAT_EXEMPT(boot);

    public:
        virtual ~Front() = default;
        [[nodiscard]] virtual DeviceBlock::Front device_front() noexcept = 0;
        [[nodiscard]] virtual infra::OptRef<app::ScanoutRelay> scanout() noexcept = 0;

        [[nodiscard]] virtual std::unique_ptr<Face> face(
            const BootParts& plat,
            DeviceBlock& device
        ) = 0;

        virtual void open_seat_fds(DeviceBlock& device) noexcept { (void)device; }

        virtual void latch(DeviceBlock& device, ThreadAssembly::SeatMains& mains) noexcept = 0;
    };

    class Tail {
        TASTY_SEAT_EXEMPT(boot);

    public:
        virtual ~Tail() = default;
        [[nodiscard]] virtual infra::OptRef<app::IOwnerTick> owner_tick() noexcept = 0;
    };

    struct Traits {

        const char* program;
        hal::FallbackNotice windows_notice;

        std::span<const char* const> boot_args;

        bool stop_ends_ladder;

        bool not_ready_quits_clean;
    };

    class Voice {
        TASTY_SEAT_EXEMPT(boot);

    public:
        virtual ~Voice() = default;
        virtual void board(
            const hal::BoardProfile& board,
            const hal::CompatibleBlob& blob
        ) const = 0;
        virtual void unknown_board(const hal::CompatibleBlob& blob) const = 0;
        virtual void irq_pins(const hal::IrqPinReport& pinned) const = 0;
        virtual void kernel(os::KernelRt kernel) const = 0;
        virtual void fabric_not_ready(const hal::BoardProfile& board) const = 0;

        virtual void wedged(std::string_view line) const noexcept = 0;

        virtual void unconfigured(const app::LinkSession& session) const = 0;
    };

    struct Refusal {
        const char* what;
        const char* cite = "";
    };

    virtual ~SessionMode() = default;

    [[nodiscard]] virtual std::unique_ptr<Front> front(const BootParts& plat) = 0;

    [[nodiscard]] virtual std::unique_ptr<Tail> tail(
        const BootParts& plat,
        DeviceBlock& device,
        ReplayBlock& replay,
        RecorderBlock& rec
    ) = 0;

    [[nodiscard]] virtual Traits traits() const noexcept = 0;
    [[nodiscard]] virtual const Voice& voice() const noexcept = 0;

    virtual void title_process() noexcept = 0;

    [[nodiscard]] virtual Exit prepare() = 0;

    [[nodiscard]] virtual int refused(const Error& e, Refusal r) = 0;

    virtual void arm_stop_signals() noexcept = 0;
    virtual void disarm_stop_signals() noexcept = 0;
    virtual void storage_ready(svc::Vfs& vfs) = 0;

    [[nodiscard]] virtual Ex<void> before_boot(app::LinkSession& session) = 0;

    [[nodiscard]] virtual infra::OptRef<const std::string> boot_handoff(
        const app::SessionOwner& owner
    ) const noexcept = 0;
    [[nodiscard]] virtual int exit_status(const Ex<void>& ran) const noexcept = 0;
};

}
