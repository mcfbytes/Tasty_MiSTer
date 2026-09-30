// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

#include "app/load_ladder.h"
#include "app/load_window_map.h"
#include "cores/core_window_decl.h"
#include "cores/ladder_host.h"
#include "cores/payload_pieces.h"
#include "cores/load_plan.h"
#include "cores/loader.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "hal/phys_region.h"
#include "svc/vfs.h"

namespace mister::app {

class LoadWalk {
    TASTY_SEAT_EXEMPT(main);

public:
    enum class Pass : std::uint8_t { Stepped, Waiting, Done, Failed };

    enum class Pc : std::uint8_t { Hold, Row, Rung, Pieces, Echo, Save, SaveWait, Release, Over };

    static constexpr std::int64_t kSaveBoundNs = 5'000'000'000;

    static constexpr std::uint16_t kPayloadChunk = 4096;

    static constexpr std::int64_t kEchoFloorNs = 2'000'000'000;
    static constexpr std::int64_t kEchoNsPerMiB = 250'000'000;
    [[nodiscard]] static constexpr std::int64_t echo_bound_ns(std::uint64_t bytes) noexcept {
        return kEchoFloorNs + static_cast<std::int64_t>((bytes * kEchoNsPerMiB) >> 20);
    }

    struct Host {
        LoadLadder::Host rung;
        cores::ILadderHost& owner;
        ILoadWindowMap* map;
        hal::PhysRegion aperture;
        const svc::Vfs& vfs;
        std::span<const cores::CoreWindowDecl> windows;
        std::int64_t now_ns = 0;
    };

    [[nodiscard]] static Ex<LoadWalk> start(std::unique_ptr<cores::ILoader> loader,
                                            const cores::LoadPlan& plan);

    [[nodiscard]] Pass step(Host& h);

    [[nodiscard]] bool wants_pass(const LinkTxChannel& inbox,
                                  const FileTxLevelCell* level) const noexcept;

    [[nodiscard]] Pc pc() const noexcept { return pc_; }
    [[nodiscard]] std::size_t row() const noexcept { return row_; }

    [[nodiscard]] bool refused() const noexcept { return plan_.frame.refused; }

    [[nodiscard]] bool stalled() const noexcept { return stalled_; }

private:
    LoadWalk(std::unique_ptr<cores::ILoader> loader, const cores::LoadPlan& plan) noexcept
        : loader_(std::move(loader)), plan_(plan) {}

    [[nodiscard]] Pass hold_(Host& h);
    [[nodiscard]] Pass row_step_(Host& h);
    [[nodiscard]] Pass window_(Host& h, const cores::TransferRow& r);
    [[nodiscard]] Pass payload_(Host& h, const cores::TransferRow& r);
    [[nodiscard]] Pass pieces_step_(Host& h);
    [[nodiscard]] Pass send_(Host& h, std::span<const std::uint8_t> bytes,
                             const cores::PostNotify& notify);
    [[nodiscard]] std::uint16_t next_act_(Host& h);
    [[nodiscard]] Pass notify_(Host& h, const cores::PostNotify& n);
    [[nodiscard]] Pass rung_(Host& h);
    [[nodiscard]] Pass echo_(Host& h);
    [[nodiscard]] Pass save_(Host& h);
    [[nodiscard]] Pass save_wait_(Host& h);
    [[nodiscard]] Pass release_(Host& h);
    [[nodiscard]] Pass fail_(Host& h);
    [[nodiscard]] Pass skip_(const cores::TransferRow& r);
    [[nodiscard]] bool status_bit0_(Host& h, bool asserted);
    void next_row_() noexcept;

    std::unique_ptr<cores::ILoader> loader_;
    cores::LoadPlan plan_;
    std::optional<LoadLadder> ladder_;
    std::optional<cores::PayloadPieces> pieces_;
    std::uint64_t echo_bytes_ = 0;
    std::size_t row_ = 0;
    std::uint16_t act_ = 0;
    std::uint32_t save_gen_ = 0;
    std::int64_t save_due_ns_ = 0;
    std::int64_t echo_due_ns_ = 0;
    bool failed_ = false;
    bool stalled_ = false;
    bool landed_ = false;
    Pc pc_ = Pc::Hold;
};

}  // namespace mister::app
