// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "infra/message_sum.h"
#include "proto/link_op.h"

namespace mister::app {

enum class OpTarget : std::uint8_t { Wire, Session, Transfer, Mount, Options, Frame, kCount };

enum class Window : std::uint8_t { None, Framed, FramedLive, FramedReady };

enum class RoundCost : std::uint8_t { Free, Payload, kCount };

enum class RoundClass : std::uint8_t { Steady, Lifecycle, kCount };

enum class RefusedAnswer : std::uint8_t { None, FtxLevel, PayloadEcho, kCount };

enum class Pad : std::uint8_t { None, Emit, Release, Stick, kCount };

enum class DatHold : std::uint8_t { Release, Arm, kCount };

using FrIds = std::array<std::string_view, 2>;

struct LinkOpFacts {
    proto::LinkOp::Kind kind;
    OpTarget target;
    Window window;
    FrIds fr;
    RoundCost round = RoundCost::Free;
    RoundClass round_class = RoundClass::Steady;
    RefusedAnswer answer = RefusedAnswer::None;
    Pad pad = Pad::None;
    DatHold dat_hold = DatHold::Release;
};

inline constexpr auto kLinkOpFacts = std::to_array<LinkOpFacts>({
    {proto::LinkOp::Kind::HoldReset, OpTarget::Wire, Window::None, {}},
    {proto::LinkOp::Kind::ReleaseReset, OpTarget::Wire, Window::None, {}},
    {proto::LinkOp::Kind::WriteStatus, OpTarget::Wire, Window::FramedLive, {}},
    {proto::LinkOp::Kind::OsdFlush, OpTarget::Wire, Window::Framed, {}},
    {proto::LinkOp::Kind::JoyEmit,
     OpTarget::Wire,
     Window::Framed,
     {},
     RoundCost::Free,
     RoundClass::Steady,
     RefusedAnswer::None,
     Pad::Emit},
    {proto::LinkOp::Kind::SendRtc, OpTarget::Wire, Window::Framed, {}},
    {proto::LinkOp::Kind::BlockAnswer, OpTarget::Wire, Window::Framed, {}},
    {proto::LinkOp::Kind::BindDecoders, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::BindSlot, OpTarget::Mount, Window::None, {}},
    {proto::LinkOp::Kind::FileTx,
     OpTarget::Transfer,
     Window::Framed,
     {},
     RoundCost::Payload,
     RoundClass::Lifecycle,
     RefusedAnswer::FtxLevel},
    {proto::LinkOp::Kind::BindFacts, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::BindConfig, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::BindUart, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::BindSlotConfig, OpTarget::Options, Window::None, {}},
    {proto::LinkOp::Kind::BindMount,
     OpTarget::Mount,
     Window::Framed,
     {},
     RoundCost::Free,
     RoundClass::Lifecycle},
    {proto::LinkOp::Kind::RebootNow, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::ApplyCore, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::AbortSwitch, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::BindIdentity, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::BindJoy, OpTarget::Wire, Window::None, {}},
    {proto::LinkOp::Kind::JoyRelease,
     OpTarget::Wire,
     Window::Framed,
     {},
     RoundCost::Free,
     RoundClass::Steady,
     RefusedAnswer::None,
     Pad::Release},
    {proto::LinkOp::Kind::SaveUpload, OpTarget::Wire, Window::None, {}},
    {proto::LinkOp::Kind::SaveAsk, OpTarget::Options, Window::None, {}},
    {proto::LinkOp::Kind::Ps2Frame, OpTarget::Wire, Window::Framed, {}},
    {proto::LinkOp::Kind::DropCore,
     OpTarget::Session,
     Window::None,
     {},
     RoundCost::Free,
     RoundClass::Lifecycle},
    {proto::LinkOp::Kind::AdoptCore, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::SessionUp,
     OpTarget::Session,
     Window::None,
     {},
     RoundCost::Free,
     RoundClass::Lifecycle},
    {proto::LinkOp::Kind::StageMount, OpTarget::Mount, Window::None, {}},
    {proto::LinkOp::Kind::StagePayload,
     OpTarget::Transfer,
     Window::Framed,
     {},
     RoundCost::Payload,
     RoundClass::Lifecycle,
     RefusedAnswer::PayloadEcho},
    {proto::LinkOp::Kind::StageReset, OpTarget::Transfer, Window::None, {}},
    {proto::LinkOp::Kind::StageDiscPayload,
     OpTarget::Transfer,
     Window::Framed,
     {},
     RoundCost::Free,
     RoundClass::Lifecycle},
    {proto::LinkOp::Kind::StageCheats, OpTarget::Mount, Window::None, {}},
    {proto::LinkOp::Kind::SetOsdVisible, OpTarget::Wire, Window::Framed, {"req", "req"}},
    {proto::LinkOp::Kind::PulseOption, OpTarget::Wire, Window::FramedLive, {}},
    {proto::LinkOp::Kind::StagedFrame, OpTarget::Frame, Window::Framed, {}, RoundCost::Payload},
    {proto::LinkOp::Kind::ApplyCheats, OpTarget::Wire, Window::Framed, {}},
    {proto::LinkOp::Kind::SetDip, OpTarget::Wire, Window::Framed, {}},
    {proto::LinkOp::Kind::SetCoreOption, OpTarget::Wire, Window::FramedLive, {}},
    {proto::LinkOp::Kind::AnnounceMount, OpTarget::Mount, Window::None, {}},
    {proto::LinkOp::Kind::SetWideIndex, OpTarget::Transfer, Window::Framed, {}},
    {proto::LinkOp::Kind::CoreReset, OpTarget::Session, Window::FramedLive, {}},
    {proto::LinkOp::Kind::LoadFacts,
     OpTarget::Session,
     Window::Framed,
     {},
     RoundCost::Free,
     RoundClass::Lifecycle},
    {proto::LinkOp::Kind::MailboxWrite, OpTarget::Session, Window::Framed, {"req", ""}},
    {proto::LinkOp::Kind::AttachSave, OpTarget::Mount, Window::None, {}},
    {proto::LinkOp::Kind::DetachSave, OpTarget::Mount, Window::None, {}},
    {proto::LinkOp::Kind::MenuCtl, OpTarget::Wire, Window::Framed, {"req", "req"}},
    {proto::LinkOp::Kind::PlainDownload,
     OpTarget::Transfer,
     Window::Framed,
     {},
     RoundCost::Payload,
     RoundClass::Lifecycle,
     RefusedAnswer::None,
     Pad::None,
     DatHold::Arm},
    {proto::LinkOp::Kind::JoyStick,
     OpTarget::Wire,
     Window::Framed,
     {},
     RoundCost::Free,
     RoundClass::Steady,
     RefusedAnswer::None,
     Pad::Stick},
});
static_assert(
    infra::rows_are_ordinal(kLinkOpFacts),
    "kLinkOpFacts is one row per LinkOp kind, in ordinal order"
);

[[nodiscard]] constexpr OpTarget target_for(proto::LinkOp::Kind k) noexcept {
    const std::size_t i = infra::ordinal(k);
    return i < kLinkOpFacts.size() ? kLinkOpFacts[i].target : OpTarget::kCount;
}

[[nodiscard]] constexpr Window window_for(proto::LinkOp::Kind k) noexcept {
    const std::size_t i = infra::ordinal(k);
    return i < kLinkOpFacts.size() ? kLinkOpFacts[i].window : Window::None;
}

[[nodiscard]] constexpr RoundCost round_cost_for(proto::LinkOp::Kind k) noexcept {
    const std::size_t i = infra::ordinal(k);
    return i < kLinkOpFacts.size() ? kLinkOpFacts[i].round : RoundCost::Free;
}

[[nodiscard]] constexpr RoundClass round_class_for(proto::LinkOp::Kind k) noexcept {
    const std::size_t i = infra::ordinal(k);
    return i < kLinkOpFacts.size() ? kLinkOpFacts[i].round_class : RoundClass::Steady;
}

static_assert(
    []() consteval {
        constexpr std::array<proto::LinkOp::Kind, 8> kLifecycle{
            proto::LinkOp::Kind::DropCore,
            proto::LinkOp::Kind::LoadFacts,
            proto::LinkOp::Kind::SessionUp,
            proto::LinkOp::Kind::FileTx,
            proto::LinkOp::Kind::StagePayload,
            proto::LinkOp::Kind::StageDiscPayload,
            proto::LinkOp::Kind::BindMount,
            proto::LinkOp::Kind::PlainDownload
        };
        for (const LinkOpFacts& row : kLinkOpFacts) {
            const bool want = std::ranges::contains(kLifecycle, row.kind);
            if (want != (row.round_class == RoundClass::Lifecycle))
                return false;
        }
        return round_class_for(proto::LinkOp::Kind::kCount) == RoundClass::Steady;
    }(),
    "exactly DropCore, LoadFacts, SessionUp, FileTx, StagePayload, StageDiscPayload, BindMount "
    "and PlainDownload are RoundClass::Lifecycle"
);

[[nodiscard]] constexpr RefusedAnswer answer_for(proto::LinkOp::Kind k) noexcept {
    const std::size_t i = infra::ordinal(k);
    return i < kLinkOpFacts.size() ? kLinkOpFacts[i].answer : RefusedAnswer::None;
}

[[nodiscard]] constexpr Pad pad_for(proto::LinkOp::Kind k) noexcept {
    const std::size_t i = infra::ordinal(k);
    return i < kLinkOpFacts.size() ? kLinkOpFacts[i].pad : Pad::None;
}

[[nodiscard]] constexpr DatHold dat_hold_for(proto::LinkOp::Kind k) noexcept {
    const std::size_t i = infra::ordinal(k);
    return i < kLinkOpFacts.size() ? kLinkOpFacts[i].dat_hold : DatHold::Release;
}

static_assert(
    []() consteval {
        constexpr std::array<proto::LinkOp::Kind, 1> kArm{proto::LinkOp::Kind::PlainDownload};
        for (const LinkOpFacts& row : kLinkOpFacts) {
            const bool want = std::ranges::contains(kArm, row.kind);
            if (want != (row.dat_hold == DatHold::Arm))
                return false;
        }
        return dat_hold_for(proto::LinkOp::Kind::kCount) == DatHold::Release;
    }(),
    "exactly PlainDownload is DatHold::Arm"
);

}
