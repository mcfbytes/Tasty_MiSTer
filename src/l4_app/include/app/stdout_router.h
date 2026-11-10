// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>

#include "app/stdout_paths.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
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

    explicit StdoutRouter(StdoutPaths paths) noexcept : paths_(paths) {}

    void set_paths(StdoutPaths paths) noexcept { paths_ = paths; }

    ~StdoutRouter();

    StdoutRouter(const StdoutRouter&) = delete;
    StdoutRouter& operator=(const StdoutRouter&) = delete;

    [[nodiscard]] Ex<void> install_initial_silence();

    [[nodiscard]] Ex<void> route(std::uint8_t debug);

    Sink sink() const noexcept { return sink_; }

private:
    StdoutPaths paths_{};

    struct FileCloser {
        void operator()(std::FILE* f) const noexcept { (void)std::fclose(f); }
    };

    using OwnedFile = std::unique_ptr<std::FILE, FileCloser>;

    infra::OptRef<std::FILE> orig_{};
    OwnedFile null_{};
    OwnedFile dbg_{};
    Sink sink_ = Sink::Original;
};

}
