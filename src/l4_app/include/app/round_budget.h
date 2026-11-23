// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>

#include "app/core_frame_counter.h"
#include "app/disk_relays.h"
#include "app/pcm/mailbox_relay.h"
#include "app/replay/replay_gate.h"
#include "app/save_upload_relay.h"
#include "app/launcher/scanout_relay.h"
#include "infra/error.h"
#include "infra/message_sum.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/link/spi_fio_queue.h"
#include "proto/types.h"

namespace mister::app {

enum class RoundAct : std::uint8_t { Disk, Mailbox, Replay, Frames, Scanout, Saves, kCount };

enum class RoundActSpend : std::uint8_t { Fixed, Gated, kCount };

struct RoundActRow {
    RoundAct act;
    RoundActSpend spend;
    proto::FioWordCount max;
    proto::FioWordCount fixed;

    proto::FioWordCount frame;
};

struct RoundActExclusion {
    RoundAct a;
    RoundAct b;
    std::string_view why;
};

inline constexpr auto kRoundActs = std::to_array<RoundActRow>({
    {RoundAct::Disk,
     RoundActSpend::Fixed,
     DiskRelays::kMaxRoundWords,
     DiskRelays::kMaxRoundWords,
     {}},
    {RoundAct::Mailbox,
     RoundActSpend::Fixed,
     pcm::MailboxRelay::kMaxRoundWords,
     pcm::MailboxRelay::kMaxRoundWords,
     pcm::MailboxRelay::kMaxBusWords},
    {RoundAct::Replay,
     RoundActSpend::Fixed,
     replay::ReplayGate::kMaxRoundWords,
     replay::ReplayGate::kMaxRoundWords,
     {}},
    {RoundAct::Frames,
     RoundActSpend::Fixed,
     CoreFrameCounter::kMaxRoundWords,
     CoreFrameCounter::kMaxRoundWords,
     {}},
    {RoundAct::Scanout,
     RoundActSpend::Fixed,
     launcher::ScanoutRelay::kMaxRoundWords,
     launcher::ScanoutRelay::kMaxRoundWords,
     {}},
    {RoundAct::Saves,
     RoundActSpend::Gated,
     SaveUploadRelay::kMaxRoundWords,
     SaveUploadRelay::kCutWords,
     {}},
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
    proto::FioWordCount sum{};
    for (std::size_t i = 0; i < kRoundActs.size(); ++i) {
        if ((Mask & (std::size_t{1} << i)) != 0)
            sum += kRoundActs[i].fixed;
    }
    return sum;
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

[[nodiscard]] consteval proto::FioWordCount widest_frame_words() noexcept {
    proto::FioWordCount widest{};
    for (const RoundActRow& row : kRoundActs) {
        if (row.frame > widest)
            widest = row.frame;
    }
    return widest;
}

[[nodiscard]] consteval bool every_gated_act_fits() noexcept {
    for (const RoundActRow& row : kRoundActs) {
        if (row.spend != RoundActSpend::Gated)
            continue;
        if (row.max > proto::link::tightest_round_budget_words())
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
    worst_round_words() <= proto::link::tightest_round_budget_words(),
    "the heaviest allowed round fits the tightest board row's round budget"
);
static_assert(
    every_gated_act_fits(),
    "a gated act's maximum fits the tightest board row's round budget"
);

static_assert(
    worst_round_words() + widest_frame_words()
        <= proto::FioWordCount{proto::link::tightest_load_budget_words()},
    "the heaviest round and its one whole frame fit the tightest board row's load budget"
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
            if (row.spend == RoundActSpend::Gated
                && infra::affine::saturating_sub(wrote, row.fixed) > left_) {
                fatal(
                    Error{Errc::contract, ERR_SITE(), 0},
                    "a gated act wrote past the budget it was offered"
                );
            }
        }
        last_act_ = act;
        debited_ = true;
        left_ = infra::affine::saturating_sub(left_, wrote);
    }

    [[nodiscard]] constexpr proto::FioWordCount remaining() const noexcept { return left_; }

private:
    proto::FioWordCount left_{};
    RoundAct last_act_{};
    bool debited_ = false;
};

}
