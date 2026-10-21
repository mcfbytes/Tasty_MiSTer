// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"
#include "ui/script_kind.h"

namespace mister::ui {

class IBtOps;

class IScriptPolicy;

class ScriptRunner {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::size_t kRingLines = 50;
    static constexpr std::size_t kLineMax = 1024;

    ScriptRunner() = default;
    ~ScriptRunner();
    ScriptRunner(const ScriptRunner&) = delete;
    ScriptRunner& operator=(const ScriptRunner&) = delete;

    [[nodiscard]] Ex<void> start(
        ScriptKind kind,
        std::string_view argv0,
        bool bt_reset = false,
        infra::OptRef<IBtOps> bt = {}
    );

    [[nodiscard]] Ex<void> pump();

    [[nodiscard]] Ex<void> cancel();

    bool finished() const noexcept { return finished_; }

    bool running() const noexcept { return child_.has_value(); }

    std::string_view title() const noexcept { return title_; }

    std::span<const std::string> lines() const noexcept;

    static bool quiesce_closes(int timeout_ms = 5000) noexcept;

private:
    void push_line(std::string_view s);
    void close_detached();
    void widen_affinity();
    void narrow_affinity();

    struct Child {
        const IScriptPolicy* policy;
        std::FILE* pipe;
    };

    std::string title_;
    bool finished_ = false;
    std::array<std::string, kRingLines> ring_{};
    std::size_t count_ = 0;
    std::optional<Child> child_{};
    char line_buf_[kLineMax] = {};
};

}
