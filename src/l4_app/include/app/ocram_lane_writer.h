// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "app/block_line.h"
#include "app/link_tx_channel.h"
#include "hal/ocram.h"
#include "hal/types.h"
#include "infra/byte_count.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "infra/seat_owner_main.h"
#include "infra/tally.h"
#include "os/epoll_set.h"
#include "proto/link/fio_open.h"
#include "proto/link_frame.h"
#include "proto/types.h"

namespace mister::app {

class OcramLaneWriter {
    TASTY_SEAT_RESIDENT(Main);

public:
    struct Wiring {
        infra::OptRef<const os::EpollSet> doorbells{};

        infra::OptRef<const LinkTxChannel<SeatTag::Main>> inbox{};
    };

    enum class Wait : std::uint8_t { None, Room, Played };

    struct Counts {
        infra::Tally frames{};
        infra::Tally rings{};
        infra::Tally busy{};
    };

    explicit OcramLaneWriter(Wiring wiring) noexcept : wiring_(wiring) {}

    OcramLaneWriter(const OcramLaneWriter&) = delete;
    OcramLaneWriter& operator=(const OcramLaneWriter&) = delete;

    [[nodiscard]] static bool servable(hal::ocram::RingDesc ring) noexcept;

    [[nodiscard]] bool bind(hal::ocram::RingLend lend, std::optional<os::UioLineRef> line) noexcept;
    void unbind() noexcept;
    [[nodiscard]] bool granted() const noexcept;

    [[nodiscard]] bool holds_writer() const noexcept { return ring_.has_value(); }

    [[nodiscard]] bool lost() const noexcept;

    [[nodiscard]] bool open(const proto::link::FioOpen& open) noexcept;

    [[nodiscard]] proto::FileSize data(std::span<const std::uint8_t> bytes, hal::Width w) noexcept;
    [[nodiscard]] bool end() noexcept;

    [[nodiscard]] bool played() noexcept;

    [[nodiscard]] bool unplayed() const noexcept;

    void wait(Wait on) noexcept;

    [[nodiscard]] bool parked() const noexcept { return waiting_ != Wait::None && line_open_; }

    [[nodiscard]] bool polls() const noexcept { return waiting_ != Wait::None && !line_open_; }

    void serve_line() noexcept;

    void close_window() noexcept;

    [[nodiscard]] const Counts& counts() const noexcept { return counts_; }

private:
    class Frame final : public proto::ILinkFrame {
    public:
        explicit Frame(OcramLaneWriter& owner) noexcept : owner_(owner) {}

        using proto::ILinkFrame::begin;
        [[nodiscard]] Ex<void> begin(proto::LinkSel sel, hal::LinkWord op) override;
        [[nodiscard]] Ex<void> word(hal::LinkWord w) override;
        [[nodiscard]] Ex<void> end() override;

        [[nodiscard]] std::optional<hal::LinkWord> opcode_reply() const noexcept override {
            TASTY_SEAT_BODY(OcramLaneWriter);
            return std::nullopt;
        }

    private:
        OcramLaneWriter& owner_;
        std::vector<hal::LinkWord> words_{};
        bool open_ = false;
    };

    [[nodiscard]] static infra::ByteCount cap_bytes_(hal::ocram::Words ring, hal::Width w) noexcept;
    [[nodiscard]] bool publish_(std::span<const hal::LinkWord> words) noexcept;

    Wiring wiring_;
    hal::ocram::RingLend lend_{};
    std::optional<hal::ocram::RingWriter<seat_owner::Main>> ring_{};
    BlockLine<seat_owner::Main> line_{};
    bool line_open_ = false;
    Wait waiting_ = Wait::None;

    std::optional<infra::Tally> pushed_at_{};
    Counts counts_{};
};

}
