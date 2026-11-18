// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "proto/block_geometry.h"
#include "proto/save_stage_types.h"
#include "proto/types.h"

namespace mister::proto {

inline constexpr std::chrono::seconds kAutosaveProbePeriod{5};
inline constexpr std::chrono::seconds kProbeAnswerWindow{1};
inline constexpr std::chrono::seconds kProbeWindowMax{3};
inline constexpr std::chrono::milliseconds kSavePassQuiet{500};
inline constexpr std::chrono::milliseconds kSaveSettlePeriod{250};

inline constexpr std::chrono::milliseconds kUserPassQuiet{150};
inline constexpr std::chrono::milliseconds kUserPassPoll{50};
inline constexpr std::chrono::seconds kAutosaveMaxDeferral{60};
inline constexpr std::chrono::seconds kSaveCommitRetry{30};
inline constexpr std::chrono::seconds kRebootCommitBound{5};
inline constexpr FileSize kStagedSaveMaxBytes{1024 * 1024};
inline constexpr FileSize kStageFirstReserve{64 * 1024};
inline constexpr SaveStageSlotCount kMaxStagedSaves{14};
inline constexpr FileSize kCommitChunkBytes{64 * 1024};
inline constexpr std::chrono::milliseconds kSavePauseDrain{500};
inline constexpr std::chrono::seconds kSaveFailedToast{4};

static_assert(kSaveSettlePeriod * 2 <= kSavePassQuiet);
static_assert(kProbeAnswerWindow >= kSavePassQuiet);
static_assert(kUserPassPoll * 2 <= kUserPassQuiet && kUserPassQuiet <= kSavePassQuiet);
static_assert(
    kAutosaveProbePeriod >= 2 * (kProbeAnswerWindow + kSavePassQuiet + kSaveSettlePeriod)
);
static_assert(kProbeAnswerWindow + kSavePassQuiet + kSaveSettlePeriod < kProbeWindowMax);
static_assert(kProbeWindowMax <= kAutosaveProbePeriod);
static_assert(kAutosaveMaxDeferral >= 2 * kAutosaveProbePeriod);
static_assert(kMaxStagedSaves == SaveStageSlotCount{2 * kAnnounceableSlots});
static_assert(kStageFirstReserve.within(kCommitChunkBytes, kStagedSaveMaxBytes));

}
