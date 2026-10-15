// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include "app/load_ladder.h"
#include "app/load_window_counts.h"
#include "app/load_window_map.h"
#include "app/path_text.h"
#include "cores/ladder_host.h"
#include "cores/manifest_stager.h"
#include "hal/phys_region.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "infra/seat_owner_main.h"

namespace mister::app {

class ManifestLadder {
    TASTY_SEAT_RESIDENT(Main);

public:
    enum class Pass : std::uint8_t { Stepped, Waiting, Done, Failed };
    enum class Pc : std::uint8_t { Next, Window, Send, Echo, Over };

    static constexpr std::uint16_t kSendChunk = 4096;

    static constexpr std::int64_t kEchoFloorNs = 2'000'000'000;
    static constexpr std::int64_t kEchoNsPerMiB = 2'000'000'000;
    [[nodiscard]] static constexpr std::int64_t echo_bound_ns(std::uint64_t bytes) noexcept {
        return kEchoFloorNs + static_cast<std::int64_t>((bytes * kEchoNsPerMiB) >> 20);
    }

    struct Host {
        LoadLadder::Host rung;
        cores::ILadderHost& owner;
        ILoadWindowMap<seat_owner::Main>& map;
        hal::PhysRegion aperture;
        LoadWindowCounts& windows;
        std::int64_t now_ns = 0;
    };

    ManifestLadder(std::unique_ptr<cores::IManifestStager> stager, PathText rel) noexcept
        : acts_(std::move(stager)), rel_(rel) {}

    [[nodiscard]] Pass step(Host& h);

    [[nodiscard]] bool wants_pass(const LinkTxChannel<SeatTag::Main>& inbox,
                                  const FileTxLevelCell* level) const noexcept;

    [[nodiscard]] Pc pc() const noexcept { return pc_; }

    [[nodiscard]] std::string_view error() const noexcept { return acts_->error(); }
    [[nodiscard]] cores::ManifestCounts counts() const noexcept { return acts_->counts(); }

    [[nodiscard]] Errc failure() const noexcept { return failure_; }

private:
    [[nodiscard]] Pass next_(Host& h);
    [[nodiscard]] Pass window_(Host& h, const cores::StageWindow& w);
    [[nodiscard]] Pass rung_(Host& h);
    [[nodiscard]] Pass send_(Host& h);
    [[nodiscard]] Pass echo_(Host& h);
    [[nodiscard]] Pass fail_(Errc why) noexcept;
    void refuse_window_(Host& h, Errc why) noexcept;

    std::unique_ptr<cores::IManifestStager> acts_;
    PathText rel_;
    Pc pc_ = Pc::Next;
    std::optional<LoadLadder> load_{};
    std::optional<cores::SendBracket> bracket_{};
    std::uint16_t act_ = 0;
    std::int64_t echo_due_ns_ = 0;
    Errc failure_ = Errc::io;
};

}  // namespace mister::app
