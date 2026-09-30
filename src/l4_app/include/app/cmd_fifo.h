// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "app/cmd_verb_sink.h"
#include "infra/fixed_str.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "infra/telemetry.h"
#include "infra/unique_fd.h"

namespace mister::app {

inline constexpr const char* kCmdFifoPath = "/dev/MiSTer_cmd";
inline constexpr std::size_t kCmdLineMax = 1024;

enum class CmdLineOutcome : std::uint8_t { Routed, Unrouted, Unrecognised };

[[nodiscard]] CmdLineOutcome deliver_cmd_line(std::string_view line, ICmdVerbSink& sink) noexcept;

struct CmdFifoStats {
    std::uint32_t reads = 0;
    std::uint32_t lines = 0;
    std::uint32_t unrouted = 0;
    std::uint32_t unrecognised = 0;
};

using CmdFifoCell = xthread::Telemetry<CmdFifoStats, SeatTag::Ui>;

class CmdFifo {
    TASTY_SEAT_RESIDENT(Ui);

public:
    static constexpr std::size_t kPathMax = 108;

    static Ex<CmdFifo> open(const char* path = kCmdFifoPath);

    ~CmdFifo();
    CmdFifo(CmdFifo&&) noexcept;
    CmdFifo& operator=(CmdFifo&&) noexcept;
    CmdFifo(const CmdFifo&) = delete;
    CmdFifo& operator=(const CmdFifo&) = delete;

    int fd() const noexcept { return fd_.get(); }
    std::string_view path() const noexcept;

    Ex<unsigned> service();

    void set_route(ICmdVerbSink* route) noexcept { route_ = route; }

    std::uint32_t routed() const noexcept { return routed_; }

    using Stats = CmdFifoStats;

    [[nodiscard]] Stats stats() const noexcept { return cell_.sample().value; }

    [[nodiscard]] const CmdFifoCell& stats_cell() const noexcept { return cell_; }

    std::string_view last_unrecognised() const noexcept;

private:
    explicit CmdFifo(UniqueFd fd, std::string_view path) noexcept;

    UniqueFd fd_;

    FixedStr<kPathMax, StrFit::Reject> path_{};
    FixedStr<kCmdLineMax, StrFit::Clip> last_bad_{};
    Stats stats_{};

    CmdFifoCell cell_{};
    ICmdVerbSink* route_ = nullptr;
    std::uint32_t routed_ = 0;
};

}  // namespace mister::app
