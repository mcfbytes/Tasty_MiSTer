// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "app/replay_stage.h"
#include "cores/movie_codec.h"
#include "infra/seat.h"

namespace mister::svc {
class IFile;
}

namespace mister::app {

class ScanStage final : public IReplayStage {
    TASTY_SEAT_RESIDENT(Ui);

public:
    [[nodiscard]] Stage level() const noexcept override;
    [[nodiscard]] Step enter(Ctx ctx) noexcept override;
    [[nodiscard]] Step tick(Ctx ctx) noexcept override;
    [[nodiscard]] Step stop(Ctx ctx) noexcept override;

private:
    struct Line {
        std::array<char, cores::IMovieCodec::kLineMax + 1> text{};
        std::uint16_t len = 0;
        bool clipped = false;
    };

    [[nodiscard]] Step read_lines_(Ctx ctx) noexcept;
    [[nodiscard]] std::optional<PlayRefusal> on_line_(
        Ctx ctx,
        std::string_view line,
        bool clipped
    ) noexcept;
    [[nodiscard]] std::optional<PlayRefusal> scan_line_(
        Ctx ctx,
        std::string_view line,
        bool clipped
    ) noexcept;
    static void note_frame_(
        ReplayPlay& play,
        std::uint32_t f,
        const cores::IMovieCodec::Frame& fr
    ) noexcept;
    [[nodiscard]] Step finish_scan_(Ctx ctx) noexcept;

    std::unique_ptr<svc::IFile> file_{};
    std::uint64_t file_size_ = 0;
    std::uint64_t off_ = 0;
    bool in_log_ = false;
    bool log_done_ = false;
    Line line_{};
};

}
