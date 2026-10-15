// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "app/event_sink.h"
#include "app/load_window_map.h"
#include "app/path_text.h"
#include "app/save_state_facts.h"
#include "cores/save_copy_level.h"
#include "hal/phys_region.h"
#include "infra/error.h"
#include "infra/fixed_str.h"
#include "infra/inbox.h"
#include "infra/loan_channel.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/seat_owner_io.h"
#include "infra/seat_owner_ui.h"
#include "infra/telemetry.h"
#include "infra/wake_flag.h"
#include "os/clock.h"
#include "proto/conf_str.h"
#include "proto/savestate_engine.h"
#include "proto/savestate_primer.h"
#include "svc/io_coworker.h"
#include "svc/vfs.h"

namespace mister::app {

class SaveStateAttach final : public svc::IIoCoworker, public proto::SaveStateEngine::ISaveToast {
    TASTY_SEAT_MEDIATOR(Ui, Io);

public:
    struct Ask {
        TASTY_SEAT_EXEMPT(component);
        proto::SaveStateDecl decl{};
        FixedStr<64, StrFit::Clip> core_name{};
        PathText rom_path{};
        bool arcade = false;
        std::uint32_t generation = 0;
    };
    using AskInbox = xthread::Inbox<Ask, 2>;

    struct PrimeOrder {
        TASTY_SEAT_MEDIATOR(Any, Any);
        proto::SaveStateDecl decl{};
        FixedStr<64, StrFit::Clip> core_name{};
        PathText rom_path{};
        bool arcade = false;
        std::uint8_t ok = 0;
        std::uint8_t aborted = 0;
        std::uint32_t abort_at = 0;
        Error err{Errc::io, 0, 0};
    };
    using Chan = xthread::LoanChannel<PrimeOrder, 1, SeatTag::Ui, SeatTag::Io>;

    struct Wiring {
        AskInbox& asks;
        SaveStateFactsCell& facts;
        cores::SaveCopyCell& copy;
        const svc::Vfs& vfs;
        ILoadWindowMap<seat_owner::Io>& map;
        ILoadWindowMap<seat_owner::Ui>& ui_map;
        hal::PhysRegion aperture;
        const os::IClock& clock;
        xthread::WakeFlag& io_wake;
        infra::OptRef<IEventSink> toast{};
    };
    explicit SaveStateAttach(const Wiring& w) noexcept;

    ~SaveStateAttach() override = default;

    void start() noexcept;

    void tick() noexcept;

    void on_ui_pause() noexcept;
    void on_save(unsigned slot) override;
    [[nodiscard]] bool live() const noexcept { return ui_.facts.live; }

    void serve() noexcept override;
    [[nodiscard]] bool idle() const noexcept override;

private:
    class Files final : public proto::SaveStateEngine::ISaveStateFiles {
        TASTY_SEAT_MEDIATOR(Ui, Io);

    public:
        explicit Files(const svc::Vfs& vfs) noexcept : vfs_(vfs) {}
        [[nodiscard]] Ex<std::size_t> read_at(std::string_view path, std::uint64_t offset,
                                              std::span<std::byte> dst) override;
        [[nodiscard]] Ex<void> write_all_sync(std::string_view path,
                                              std::span<const std::byte> src) override;

    private:
        const svc::Vfs& vfs_;
    };

    class IoSlotMap final : public proto::SaveStateEngine::ISlotMap {
        TASTY_SEAT_RESIDENT(Io);

    public:
        explicit IoSlotMap(ILoadWindowMap<seat_owner::Io>& map) noexcept : map_(map) {}
        [[nodiscard]] Ex<hal::FpgaMemory> map(const hal::PhysRegion& r) override {
            TASTY_SEAT_BODY(IoSlotMap);
            return map_.map(r);
        }

    private:
        ILoadWindowMap<seat_owner::Io>& map_;
    };
    class UiSlotMap final : public proto::SaveStateEngine::ISlotMap {
        TASTY_SEAT_RESIDENT(Ui);

    public:
        explicit UiSlotMap(ILoadWindowMap<seat_owner::Ui>& map) noexcept : map_(map) {}
        [[nodiscard]] Ex<hal::FpgaMemory> map(const hal::PhysRegion& r) override {
            TASTY_SEAT_BODY(UiSlotMap);
            return map_.map(r);
        }

    private:
        ILoadWindowMap<seat_owner::Ui>& map_;
    };

    void take_ask_(const Ask& ask) noexcept;
    void reap_() noexcept;
    void scan_() noexcept;
    void answer_(bool attached) noexcept;
    void finish_(bool ok, Error err) noexcept;

    AskInbox& asks_;
    SaveStateFactsCell& facts_cell_;
    cores::SaveCopyCell& copy_;
    infra::OptRef<IEventSink> toast_;
    Files files_;
    IoSlotMap io_map_;
    UiSlotMap ui_map_;
    Chan chan_;

    xthread::Telemetry<std::uint32_t, SeatTag::Ui> abort_{};

    struct UiHalf {
        UiHalf(const hal::PhysRegion& aperture, const proto::SaveStateEngine::Environment& env)
            : engine(aperture, env) {}
        proto::SaveStateEngine engine;
        bool out = false;
        bool stale = false;
        bool raised = false;
        std::uint32_t held = 0;
        std::uint32_t abort_level = 0;
        SaveStateFacts facts{};
    } ui_;

    struct IoHalf {
        proto::SavePrimer primer;
        Chan::Job job{};
    } io_;
};

}  // namespace mister::app
