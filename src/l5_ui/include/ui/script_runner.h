// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "infra/unique_fd.h"

namespace mister::ui {

class ScriptRequest;

class ScriptRunner {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::size_t kRingLines = 50;
    static constexpr std::size_t kLineMax = 1024;

    ScriptRunner() = default;
    ~ScriptRunner();
    ScriptRunner(const ScriptRunner&) = delete;
    ScriptRunner& operator=(const ScriptRunner&) = delete;

    [[nodiscard]] Ex<void> start(const ScriptRequest& req);

    [[nodiscard]] Ex<void> pump();

    [[nodiscard]] Ex<void> cancel();

    bool finished() const noexcept { return finished_; }

    bool running() const noexcept { return child_.has_value(); }

    std::string_view title() const noexcept { return title_; }

    std::span<const std::string> lines() const noexcept;

    static bool quiesce_closes(
        std::chrono::milliseconds timeout = std::chrono::milliseconds{5000}
    ) noexcept;

private:
    void push_line(std::string_view s);
    void close_detached();
    void widen_affinity();
    void narrow_affinity();

    struct Child {
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
