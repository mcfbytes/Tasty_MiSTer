// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "app/core_frame_counter.h"
#include "app/disk_relay.h"
#include "app/mailbox_relay.h"
#include "app/replay_gate.h"
#include "app/save_upload_relay.h"
#include "app/scanout_relay.h"
#include "infra/error.h"
#include "infra/message_sum.h"
#include "infra/seat.h"
#include "proto/spi_fio_queue.h"
#include "proto/types.h"

namespace mister::app {

enum class RoundAct : std::uint8_t { Disk, Mailbox, Replay, Frames, Scanout, Saves, kCount };

enum class RoundActSpend : std::uint8_t { Fixed, Gated, kCount };

struct RoundActRow {
    RoundAct act;
    RoundActSpend spend;
    proto::FioWordCount max;
    proto::FioWordCount fixed;
};

struct RoundActExclusion {
    RoundAct a;
    RoundAct b;
    std::string_view why;
};

inline constexpr auto kRoundActs = std::to_array<RoundActRow>({
    {RoundAct::Disk,
     RoundActSpend::Fixed,
     proto::FioWordCount{DiskRelay::kMaxRoundWords},
     proto::FioWordCount{DiskRelay::kMaxRoundWords}},
    {RoundAct::Mailbox,
     RoundActSpend::Fixed,
     MailboxRelay::kMaxRoundWords,
     MailboxRelay::kMaxRoundWords},
    {RoundAct::Replay,
     RoundActSpend::Fixed,
     proto::FioWordCount{ReplayGate::kMaxRoundWords},
     proto::FioWordCount{ReplayGate::kMaxRoundWords}},
    {RoundAct::Frames,
     RoundActSpend::Fixed,
     proto::FioWordCount{CoreFrameCounter::kMaxRoundWords},
     proto::FioWordCount{CoreFrameCounter::kMaxRoundWords}},
    {RoundAct::Scanout,
     RoundActSpend::Fixed,
     proto::FioWordCount{ScanoutRelay::kMaxRoundWords},
     proto::FioWordCount{ScanoutRelay::kMaxRoundWords}},
    {RoundAct::Saves,
     RoundActSpend::Gated,
     proto::FioWordCount{SaveUploadRelay::kMaxRoundWords},
     SaveUploadRelay::kCutWords},
});

inline constexpr auto kRoundActExclusions = std::to_array<RoundActExclusion>({
    {RoundAct::Disk, RoundAct::Replay, "no movie codec exists for a core with a disk relay"},
});

[[nodiscard]] constexpr const RoundActRow& round_act_row(RoundAct act) noexcept {
    for (const RoundActRow& row : kRoundActs) {
        if (row.act == act)
            return row;
    }
    return kRoundActs.front();
}

namespace detail {

inline constexpr proto::FioWordCount kSumCap{proto::SpiFioQueue::kUnbudgeted};

[[nodiscard]] consteval bool round_acts_have_one_row_each() noexcept {
    for (std::size_t i = 0; i < kRoundActs.size(); ++i) {
        for (std::size_t j = i + 1; j < kRoundActs.size(); ++j) {
            if (kRoundActs[i].act == kRoundActs[j].act)
                return false;
        }
    }
    return true;
}

template <std::size_t Mask>
[[nodiscard]] consteval bool excluded() noexcept {
    for (const RoundActExclusion& x : kRoundActExclusions) {
        bool a = false;
        bool b = false;
        for (std::size_t i = 0; i < kRoundActs.size(); ++i) {
            if ((Mask & (std::size_t{1} << i)) == 0)
                continue;
            a = a || kRoundActs[i].act == x.a;
            b = b || kRoundActs[i].act == x.b;
        }
        if (a && b)
            return true;
    }
    return false;
}

template <std::size_t Mask>
[[nodiscard]] consteval proto::FioWordCount subset_words() noexcept {
    proto::FioWordCount left = kSumCap;
    for (std::size_t i = 0; i < kRoundActs.size(); ++i) {
        if ((Mask & (std::size_t{1} << i)) != 0)
            left = left.saturating_sub(kRoundActs[i].fixed);
    }
    return kSumCap.saturating_sub(left);
}

template <std::size_t Mask>
[[nodiscard]] consteval proto::FioWordCount allowed_words() noexcept {
    return excluded<Mask>() ? proto::FioWordCount{} : subset_words<Mask>();
}

template <std::size_t... Masks>
[[nodiscard]] consteval proto::FioWordCount heaviest(std::index_sequence<Masks...>) noexcept {
    proto::FioWordCount worst{};
    ((worst = allowed_words<Masks>() > worst ? allowed_words<Masks>() : worst), ...);
    return worst;
}

}

[[nodiscard]] consteval proto::FioWordCount worst_round_words() noexcept {
    return detail::heaviest(std::make_index_sequence<(1u << kRoundActs.size())>{});
}

[[nodiscard]] consteval bool every_gated_act_fits() noexcept {
    for (const RoundActRow& row : kRoundActs) {
        if (row.spend != RoundActSpend::Gated)
            continue;
        if (row.max > proto::FioWordCount{proto::tightest_round_budget_words()})
            return false;
    }
    return true;
}

static_assert(detail::round_acts_have_one_row_each(), "kRoundActs: one row per act");
static_assert(
    kRoundActs.size() == infra::ordinal(RoundAct::kCount),
    "kRoundActs: a row for every act"
);
static_assert(
    worst_round_words() <= proto::FioWordCount{proto::tightest_round_budget_words()},
    "the heaviest allowed round fits the tightest board row's round budget"
);
static_assert(
    every_gated_act_fits(),
    "a gated act's maximum fits the tightest board row's round budget"
);

class RoundBudget final {
public:
    explicit constexpr RoundBudget(proto::FioWordCount whole) noexcept : left_(whole) {}

    constexpr void debit(RoundAct act, proto::FioWordCount wrote) noexcept {
        const RoundActRow& row = round_act_row(act);
        if constexpr (kSeatChecksEnabled) {
            if (debited_ && act < last_act_) {
                fatal(
                    Error{Errc::contract, ERR_SITE(), 0},
                    "FIO act debited out of RoundAct order"
                );
            }
            if (row.spend == RoundActSpend::Gated && wrote.saturating_sub(row.fixed) > left_) {
                fatal(
                    Error{Errc::contract, ERR_SITE(), 0},
                    "a gated act wrote past the budget it was offered"
                );
            }
        }
        last_act_ = act;
        debited_ = true;
        left_ = left_.saturating_sub(wrote);
    }

    [[nodiscard]] constexpr proto::FioWordCount remaining() const noexcept { return left_; }

private:
    proto::FioWordCount left_{};
    RoundAct last_act_{};
    bool debited_ = false;
};

}
