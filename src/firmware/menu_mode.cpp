// SPDX-License-Identifier: GPL-3.0-or-later
#include "menu_mode.h"

#include <unistd.h>

#include <cstdio>
#include <memory>
#include <string_view>

#include "app/link_session.h"
#include "app/session_owner.h"
#include "hal/boards_table.h"
#include "menu_block.h"
#include "report.h"

extern char** environ;

namespace mister::fw {

namespace {

const char* irq_pin_verdict(hal::IrqPinOutcome o) {
    using O = hal::IrqPinOutcome;
    switch (o) {
        case O::Applied:
            return "applied";
        case O::NoInterruptsFile:
            return "no readable interrupts list";
        case O::ListTruncated:
            return "interrupts list outgrew the read buffer";
        case O::NoSuchAction:
            return "no interrupt line names this action";
        case O::WriteRefused:
            return "smp_affinity refused the write";
        case O::ReadbackUnreadable:
            return "smp_affinity unreadable after the write";
        case O::ReadbackMismatch:
            return "the kernel kept a different mask";
    }
    return "?";
}

void announce_irq_pin(const hal::IrqPinResult& r) {
    const int alen = static_cast<int>(r.action.size());
    if (r.outcome == hal::IrqPinOutcome::Applied) {
        std::fprintf(
            stderr,
            "mister: irq %u (%.*s) confined to cpus 0x%x",
            r.irq.v,
            alen,
            r.action.data(),
            r.read_back.v
        );
        if (r.effective_known)
            std::fprintf(stderr, ", effective 0x%x", r.effective.v);
        std::fputc('\n', stderr);
        return;
    }
    std::fprintf(
        stderr,
        "mister: irq pin %.*s NOT applied: %s",
        alen,
        r.action.data(),
        irq_pin_verdict(r.outcome)
    );
    if (r.err != 0)
        std::fprintf(stderr, " (errno %d)", r.err);
    std::fputs("; boot continues\n", stderr);
}

}

MenuMode::MenuMode(int argc, char** argv) noexcept
    : title_(os::ProcessTitle::create(argc, argv, environ)) {

    if (!title_)
        report("ProcessTitle::create", title_.error());

    boot_argv_ = {argv[0], (argc > 1) ? argv[1] : "", nullptr};
    if (argc > 2) {
        boot_argv_[2] = argv[2];
        boot_argc_ = 3;
    }
}

std::unique_ptr<SessionMode::Front> MenuMode::front(const BootParts& plat) {
    return std::make_unique<Launcher>(
        plat,
        title_ ? infra::OptRef<os::ProcessTitle>{*title_} : infra::OptRef<os::ProcessTitle>{}
    );
}

std::unique_ptr<SessionMode::Tail> MenuMode::tail(
    const BootParts&,
    DeviceBlock&,
    ReplayBlock&,
    RecorderBlock&
) {
    return std::make_unique<NoTail>();
}

std::unique_ptr<SessionMode::Face> MenuMode::Launcher::face(
    const BootParts& plat,
    DeviceBlock& device
) {
    return std::make_unique<MenuBlock>(plat, device, launcher_, title_);
}

void MenuMode::Launcher::open_seat_fds(DeviceBlock& device) noexcept {
    launcher_.open_fds(device.verdict());
}

void MenuMode::Launcher::latch(DeviceBlock& device, ThreadAssembly::SeatMains& mains) noexcept {
    if (device.owner_.launcher())
        launcher_.open_seat(device, mains);
}

int MenuMode::refused(const Error& e, Refusal r) {
    char what[256];
    (void)std::snprintf(what, sizeof what, "%s%s", r.what, r.cite);
    fatal(e, what);
}

void MenuMode::StderrVoice::board(
    const hal::BoardProfile& board,
    const hal::CompatibleBlob& blob
) const {
    for (const std::string_view s : board.compatible) {
        if (!blob.contains(s))
            continue;
        std::fprintf(
            stderr,
            "mister: board %.*s (compatible \"%.*s\")%s\n",
            static_cast<int>(board.model.size()),
            board.model.data(),
            static_cast<int>(s.size()),
            s.data(),
            board.verified ? "" : " [UNVERIFIED row]"
        );
        break;
    }
    for (std::size_t i = 0; i < hal::kFactFieldCount; ++i) {
        const auto f = static_cast<hal::FactField>(i);
        if (hal::fact_measured(board, f))
            continue;
        const std::string_view name = hal::fact_field_name(f);
        std::fprintf(
            stderr,
            "mister: board %.*s: %.*s is Unmeasured; a reader refuses it, none guesses\n",
            static_cast<int>(board.model.size()),
            board.model.data(),
            static_cast<int>(name.size()),
            name.data()
        );
    }
}

void MenuMode::StderrVoice::unknown_board(const hal::CompatibleBlob& blob) const {
    std::fprintf(stderr, "mister: no board profile claims this device tree.\n  compatible:");
    for (const char c : blob.raw()) {
        std::fputc(c == '\0' ? ' ' : c, stderr);
    }
    std::fprintf(stderr, "\n  known boards:\n");
    for (const hal::BoardProfile* p : hal::board_table()) {
        std::fprintf(stderr, "    %.*s:", static_cast<int>(p->model.size()), p->model.data());
        for (const std::string_view s : p->compatible) {
            std::fprintf(stderr, " %.*s", static_cast<int>(s.size()), s.data());
        }
        std::fputc('\n', stderr);
    }
}

void MenuMode::StderrVoice::irq_pins(const hal::IrqPinReport& pinned) const {
    if (pinned.table_refused) {
        std::fprintf(
            stderr,
            "mister: irq pins: %zu rows declared, table REFUSED whole "
            "(at most %zu, none on T-RT's core); nothing written\n",
            pinned.declared,
            hal::kMaxIrqPins
        );
    }
    for (std::size_t i = 0; i < pinned.count; ++i)
        announce_irq_pin(pinned.pins[i]);
}

void MenuMode::disarm_stop_signals() noexcept {
    if (stop_signals_)
        stop_signals_->disarm();
}

void MenuMode::StderrVoice::fabric_not_ready(const hal::BoardProfile& board) const {
    if (!board.verified) {

        std::fprintf(
            stderr,
            "mister: board %.*s has no link port yet (unverified row); quitting\n",
            static_cast<int>(board.model.size()),
            board.model.data()
        );
        return;
    }
    std::printf("\nGPI[31]==1. FPGA is uninitialized or incompatible core loaded.\n");
    std::printf("Quitting. Bye bye...\n");
}

void MenuMode::storage_ready(svc::Vfs& vfs) {

    if (auto d = vfs.ensure_dir("config"); !d) {
        report("ensure_dir(config)", d.error());
    }
}

void MenuMode::StderrVoice::kernel(os::KernelRt kernel) const {
    std::fprintf(
        stderr,
        "mister: kernel: %s\n",
        kernel == os::KernelRt::PreemptRt   ? "RT"
            : kernel == os::KernelRt::NotRt ? "non-RT"
                                            : "unknown"
    );
}

Ex<void> MenuMode::before_boot(app::LinkSession& session) {
    app::StdoutRouter* router = session.boot_stdout_router();
    if (router == nullptr)
        return {};
    return router->install_initial_silence();
}

infra::OptRef<const std::string> MenuMode::boot_handoff(
    const app::SessionOwner& owner
) const noexcept {
    const std::string& exe = owner.boot_handoff();
    if (exe.empty())
        return {};
    return infra::OptRef<const std::string>{exe};
}

void MenuMode::StderrVoice::wedged(std::string_view line) const noexcept {
    const auto w = ::write(STDERR_FILENO, line.data(), line.size());
    (void)w;
}

void MenuMode::StderrVoice::unconfigured(const app::LinkSession& sup) const {
    if (!sup.fpga_unconfigured())
        return;
    if (sup.ever_running()) {
        std::fprintf(
            stderr,
            "mister: FPGA unconfigured and the warm reboot request "
            "did NOT take; power-cycle required or the stock binary "
            "will refuse to boot\n"
        );
        return;
    }

    std::fprintf(
        stderr,
        "mister: FPGA left unconfigured by a failed bitstream program "
        "at BOOT; no automatic reboot is taken for a boot-time "
        "failure; power-cycle (or reboot) required or the stock "
        "binary will refuse to boot\n"
    );
}

}
