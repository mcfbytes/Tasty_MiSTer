// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/prelude.h"
#include "infra/seat.h"
#include "proto/types.h"
#include "svc/disc/chd_prefetch.h"
#include "svc/disc/cue_policy.h"
#include "svc/disc/disc_engine.h"
#include "svc/disc/disc_reader.h"
#include "svc/vfs.h"

namespace mister::cores::ide {

inline constexpr svc::disc::CuePolicy kAtapiCuePolicy{
    .type_detect = svc::disc::CuePolicy::TypeDetect::PerTrack,
    .offset_op = svc::disc::CuePolicy::OffsetOp::Assign,
    .sub_naming = svc::disc::CuePolicy::SubNaming::None,
    .program = svc::disc::CuePolicy::Program::SentinelExclusive,
};

class AtapiDisc final : public svc::disc::IDiscReader {
    TASTY_SEAT_RESIDENT(Io);

public:
    [[nodiscard]] static Ex<std::unique_ptr<AtapiDisc>> open(
        const svc::Vfs& vfs,
        std::string_view path
    );

    [[nodiscard]] Ex<std::span<std::byte>> read_form(
        svc::disc::DiscForm form,
        proto::Lba lba,
        std::span<std::byte> dst,
        std::optional<svc::disc::SectorHeader> header = {}
    ) override;

    [[nodiscard]] const svc::disc::Toc& toc() const noexcept { return engine_->toc(); }

private:
    AtapiDisc() = default;
    [[nodiscard]] Ex<void> mount_(const svc::Vfs& vfs, std::string_view path);

    svc::disc::ChdPrefetch prefetch_{};
    std::optional<svc::disc::DiscEngine> engine_{};
};

}
