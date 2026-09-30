// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "infra/seat.h"

namespace mister::app {

class StdoutRouter {
    TASTY_SEAT_RESIDENT(RT);

public:
    enum class Sink : std::uint8_t { Original, Null, DebugFile };

    static constexpr Sink sink_for(std::uint8_t debug) noexcept {
        return debug == 2 ? Sink::DebugFile : debug != 0 ? Sink::Original : Sink::Null;
    }

    StdoutRouter() noexcept = default;

    StdoutRouter(const char* null_path, const char* debug_path) noexcept
        : null_path_(null_path), debug_path_(debug_path) {}

    void set_paths(const char* null_path, const char* debug_path) noexcept {
        null_path_ = null_path;
        debug_path_ = debug_path;
    }
    ~StdoutRouter();

    StdoutRouter(const StdoutRouter&) = delete;
    StdoutRouter& operator=(const StdoutRouter&) = delete;

    [[nodiscard]] Ex<void> install_initial_silence();

    [[nodiscard]] Ex<void> route(std::uint8_t debug);

    Sink sink() const noexcept { return sink_; }

private:
    const char* null_path_ = "/dev/null";
    const char* debug_path_ = "/tmp/debug.txt";
    void* orig_ = nullptr;
    void* null_ = nullptr;
    void* dbg_ = nullptr;
    Sink sink_ = Sink::Original;
};

}  // namespace mister::app
