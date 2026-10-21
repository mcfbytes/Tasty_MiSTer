// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/script_runner.h"

#include <fcntl.h>
#include <pthread.h>
#include <time.h>

#include <atomic>
#include <cerrno>
#include <cstring>

#include "infra/persist.h"

#include "app/process_integration.h"
#include "ui/bt_ops.h"
#include "ui/fb_terminal_host.h"
#include "ui/script_policy.h"
#include "ui/shell_bt_ops.h"

namespace mister::ui {

static std::atomic<unsigned> g_closes_in_flight TASTY_PERSIST(proc, script_closes_in_flight){0};

namespace {

extern "C" void* close_pipe_thread(void* arg) {
    std::FILE* p = static_cast<std::FILE*>(arg);
    ::pclose(p);
    g_closes_in_flight.fetch_sub(1, std::memory_order_release);
    return nullptr;
}

}

ScriptRunner::~ScriptRunner() {

    const bool live = child_.has_value() && !finished_;
    close_detached();

    if (live)
        narrow_affinity();
}

Ex<bool> ShellBtOps::adapter_up() {
    TASTY_SEAT_BODY(ShellBtOps);
    return app::proc::bt_adapter_up();
}

Ex<void> ShellBtOps::reset_adapter() {
    TASTY_SEAT_BODY(ShellBtOps);
    return app::proc::hciconfig_reset();
}

Ex<void> ShellBtOps::renew_pairings() {
    TASTY_SEAT_BODY(ShellBtOps);

    return app::proc::bluetoothd_renew();
}

Ex<void> ScriptRunner::start(
    ScriptKind kind,
    std::string_view argv0,
    bool bt_reset,
    infra::OptRef<IBtOps> bt
) {
    if (child_) {
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    }
    const IScriptPolicy& policy = script_policy_for(kind);
    finished_ = false;
    count_ = 0;
    for (auto& s : ring_)
        s.clear();

    auto spawn = policy.resolve(argv0);
    if (!spawn)
        return std::unexpected(spawn.error());
    const std::string cmd = static_cast<std::string&&>(spawn->cmd);
    title_ = static_cast<std::string&&>(spawn->title);

    widen_affinity();

    policy.before_spawn(bt_reset, bt);

    std::FILE* p = ::popen(cmd.c_str(), "r");
    if (p == nullptr) {
        narrow_affinity();
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    const int fd = ::fileno(p);
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    child_.emplace(Child{&policy, p});
    return {};
}

Ex<void> ScriptRunner::pump() {
    if (!child_ || finished_)
        return {};
    std::FILE* const pipe = child_->pipe;

    if (std::feof(pipe) == 0) {

        if (std::fgets(line_buf_, sizeof line_buf_, pipe) != nullptr) {
            std::string_view s{line_buf_};
            if (const auto nl = s.find('\n'); nl != std::string_view::npos)
                s = s.substr(0, nl);
            push_line(s);
        }
        return {};
    }

    close_detached();
    narrow_affinity();
    finished_ = true;
    return {};
}

Ex<void> ScriptRunner::cancel() {
    if (!child_ || finished_)
        return {};

    child_->policy->stop(title_);

    close_detached();
    narrow_affinity();
    finished_ = true;
    return {};
}

std::span<const std::string> ScriptRunner::lines() const noexcept {
    return {ring_.data(), count_};
}

void ScriptRunner::push_line(std::string_view s) {
    if (s.size() >= kLineMax)
        s = s.substr(0, kLineMax - 1);
    if (count_ < kRingLines) {
        ring_[count_++].assign(s);
        return;
    }

    for (std::size_t i = 1; i < kRingLines; ++i) {
        ring_[i - 1] = static_cast<std::string&&>(ring_[i]);
    }
    ring_[kRingLines - 1].assign(s);
}

void ScriptRunner::close_detached() {
    if (!child_)
        return;
    std::FILE* const p = child_->pipe;
    child_.reset();
    g_closes_in_flight.fetch_add(1, std::memory_order_relaxed);
    ::pthread_t tid;
    if (::pthread_create(&tid, nullptr, close_pipe_thread, p) == 0) {
        ::pthread_detach(tid);
    } else {

        ::pclose(p);
        g_closes_in_flight.fetch_sub(1, std::memory_order_release);
    }
}

bool ScriptRunner::quiesce_closes(int timeout_ms) noexcept {
    const ::timespec tick{0, 1'000'000};
    for (int waited = 0; waited <= timeout_ms; ++waited) {
        if (g_closes_in_flight.load(std::memory_order_acquire) == 0)
            return true;
        (void)::nanosleep(&tick, nullptr);
    }
    return g_closes_in_flight.load(std::memory_order_acquire) == 0;
}

void ScriptRunner::widen_affinity() {
    app::proc::widen_ui_affinity();
}

void ScriptRunner::narrow_affinity() {
    app::proc::restore_ui_affinity();
}

Ex<void> FbTerminalHost::launch(std::string_view) {
    return unimplemented(ERR_SITE());
}

Ex<bool> FbTerminalHost::reap() {
    return unimplemented(ERR_SITE());
}

}
