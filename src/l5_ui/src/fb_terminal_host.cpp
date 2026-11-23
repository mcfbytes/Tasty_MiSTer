// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/fb_terminal_host.h"

#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "infra/error.h"
#include "infra/prelude.h"
#include "os/file_create.h"
#include "os/write_all.h"

namespace mister::ui {

FbTerminalHost::FbTerminalHost(Wiring w) noexcept : w_(static_cast<Wiring&&>(w)) {}

bool FbTerminalHost::write_script_(const TerminalScript& script) noexcept {
    const char* path = w_.recipe.script_path().c_str();
    (void)::unlink(path);

    auto fd = os::open_created(
        path,
        os::OpenFlags::write_only().creating().exclusive(),
        kTerminalScriptMode
    );
    if (!fd)
        return false;
    return os::write_all(*fd, std::as_bytes(std::span(script.text()))).has_value();
}

Ex<void> FbTerminalHost::launch(const TerminalScript& script) noexcept {
    TASTY_SEAT_BODY(FbTerminalHost);
    if (phase_ == TerminalPhase::Starting
        || phase_ == TerminalPhase::Running
        || (phase_ == TerminalPhase::Detached && child_running())) {
        return std::unexpected(Error{Errc::busy, ERR_SITE(), 0});
    }
    if (!write_script_(script)) {
        phase_ = TerminalPhase::Failed;
        return std::unexpected(infra::errno_error(Errc::io, ERR_SITE(), errno));
    }
    auto lease = w_.fb.raise(app::FbTenant::Terminal);
    if (!lease) {
        phase_ = TerminalPhase::Failed;
        return std::unexpected(lease.error());
    }
    lease_ = *lease;
    drop_owed_ = false;
    applies_seen_ = w_.video_stats.sample().value.applies;
    if (auto asked = w_.vt.activate(kTerminalVt); !asked) {
        drop_lease_();
        phase_ = TerminalPhase::Failed;
        return std::unexpected(asked.error());
    }
    settle_by_ = w_.clock.now() + std::chrono::nanoseconds{os::kVtSettle};
    phase_ = TerminalPhase::Starting;
    return {};
}

void FbTerminalHost::spawn_() noexcept {
    std::vector<const char*> argv;
    argv.reserve(w_.recipe.argv().size());
    for (const std::string& arg : w_.recipe.argv())
        argv.push_back(arg.c_str());
    std::vector<const char*> envp;
    for (char** e = environ; e != nullptr && *e != nullptr; ++e)
        envp.push_back(*e);
    auto child = os::ChildProcess::spawn(
        {.argv = argv, .envp = envp, .tty = w_.recipe.tty(), .kept_fd = -1}
    );
    if (!child) {
        drop_lease_();
        phase_ = TerminalPhase::Failed;
        return;
    }
    child_.emplace(std::move(*child));
    phase_ = TerminalPhase::Running;
}

void FbTerminalHost::reap_() noexcept {
    if (!child_.has_value())
        return;
    if (!child_->try_reap())
        return;
    child_.reset();
    if (phase_ == TerminalPhase::Running)
        phase_ = TerminalPhase::Ended;
    else if (phase_ == TerminalPhase::Detached)
        phase_ = TerminalPhase::Idle;
}

void FbTerminalHost::drop_lease_() noexcept {
    if (!lease_) {
        drop_owed_ = false;
        return;
    }
    if (w_.fb.drop(*lease_) || !w_.fb.holds(*lease_)) {
        lease_.reset();
        drop_owed_ = false;
        return;
    }
    drop_owed_ = true;
}

void FbTerminalHost::retry_drop_() noexcept {
    if (!drop_owed_)
        return;
    if (!lease_) {
        drop_owed_ = false;
        return;
    }
    if (w_.fb.drop(*lease_) || !w_.fb.holds(*lease_)) {
        lease_.reset();
        drop_owed_ = false;
    }
}

void FbTerminalHost::reraise_() noexcept {
    if (auto lease = w_.fb.raise(app::FbTenant::Terminal); lease)
        lease_ = *lease;
}

void FbTerminalHost::serve() noexcept {
    TASTY_SEAT_BODY(FbTerminalHost);
    retry_drop_();
    reap_();
    if (phase_ == TerminalPhase::Starting) {

        if (lease_ && !w_.fb.holds(*lease_)) {
            detach();
            return;
        }
        if (w_.vt.active() == kTerminalVt) {
            spawn_();
            return;
        }
        if (w_.clock.now() >= settle_by_) {
            drop_lease_();
            phase_ = TerminalPhase::Failed;
        }
        return;
    }
    if (lease_ && !w_.fb.holds(*lease_)) {
        detach();
        return;
    }
    if (lease_ && w_.video_stats.sample().value.applies != applies_seen_) {
        applies_seen_ = w_.video_stats.sample().value.applies;
        reraise_();
    }
}

void FbTerminalHost::finish() noexcept {
    TASTY_SEAT_BODY(FbTerminalHost);
    if (child_running()) {
        detach();
        return;
    }
    child_.reset();
    drop_lease_();
    phase_ = TerminalPhase::Idle;
}

void FbTerminalHost::detach() noexcept {
    TASTY_SEAT_BODY(FbTerminalHost);
    drop_lease_();
    if (child_running()) {
        phase_ = TerminalPhase::Detached;
        return;
    }
    child_.reset();
    phase_ = TerminalPhase::Idle;
}

void FbTerminalHost::on_pause() noexcept {
    TASTY_SEAT_BODY(FbTerminalHost);
    detach();
}

void FbTerminalHost::on_stop() noexcept {
    TASTY_SEAT_BODY(FbTerminalHost);
    drop_lease_();
    if (child_.has_value()) {
        child_->abandon();
        child_.reset();
    }
    phase_ = TerminalPhase::Idle;
}

}
