// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "infra/message_sum.h"
#include "proto/link_op.h"

namespace mister::app {

enum class OpTarget : std::uint8_t { Wire, Session, kCount };

enum class Window : std::uint8_t { None, Framed, FramedLive };

using FrIds = std::array<std::string_view, 2>;

struct LinkOpFacts {
    proto::LinkOp::Kind kind;
    OpTarget target;
    Window window;
    FrIds fr;
};

inline constexpr auto kLinkOpFacts = std::to_array<LinkOpFacts>({
    {proto::LinkOp::Kind::HoldReset, OpTarget::Wire, Window::None, {}},
    {proto::LinkOp::Kind::ReleaseReset, OpTarget::Wire, Window::None, {}},
    {proto::LinkOp::Kind::WriteStatus, OpTarget::Wire, Window::FramedLive, {}},
    {proto::LinkOp::Kind::OsdFlush, OpTarget::Wire, Window::Framed, {}},
    {proto::LinkOp::Kind::JoyEmit, OpTarget::Wire, Window::Framed, {}},
    {proto::LinkOp::Kind::SendRtc, OpTarget::Wire, Window::Framed, {}},
    {proto::LinkOp::Kind::BlockAnswer, OpTarget::Wire, Window::Framed, {}},
    {proto::LinkOp::Kind::BindDecoders, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::BindSlot, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::FileTx, OpTarget::Session, Window::Framed, {}},
    {proto::LinkOp::Kind::BindFacts, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::BindConfig, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::BindUart, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::BindSlotConfig, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::BindMount, OpTarget::Session, Window::Framed, {}},
    {proto::LinkOp::Kind::BindSlotPreviews, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::RebootNow, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::ApplyCore, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::AbortSwitch, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::BindIdentity, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::BindDoorbells, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::BindJoy, OpTarget::Wire, Window::None, {}},
    {proto::LinkOp::Kind::JoyRelease, OpTarget::Wire, Window::Framed, {}},
    {proto::LinkOp::Kind::SaveUpload, OpTarget::Wire, Window::None, {}},
    {proto::LinkOp::Kind::SaveAsk, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::Ps2Frame, OpTarget::Wire, Window::Framed, {}},
    {proto::LinkOp::Kind::DropCore, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::MakeCore, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::SessionUp, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::StageMount, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::StagePayload, OpTarget::Session, Window::Framed, {}},
    {proto::LinkOp::Kind::StageReset, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::StageDiscPayload, OpTarget::Session, Window::Framed, {}},
    {proto::LinkOp::Kind::StageCheats, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::SetOsdVisible, OpTarget::Wire, Window::Framed, {"req", "req"}},
    {proto::LinkOp::Kind::PulseOption, OpTarget::Wire, Window::FramedLive, {}},
    {proto::LinkOp::Kind::SetVideoMode, OpTarget::Wire, Window::Framed, {}},
    {proto::LinkOp::Kind::ApplyCheats, OpTarget::Wire, Window::Framed, {}},
    {proto::LinkOp::Kind::SetDip, OpTarget::Wire, Window::Framed, {}},
    {proto::LinkOp::Kind::SetCoreOption, OpTarget::Wire, Window::FramedLive, {}},
    {proto::LinkOp::Kind::SettleCoreOptions, OpTarget::Wire, Window::FramedLive, {}},
    {proto::LinkOp::Kind::SetVolume, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::AnnounceMount, OpTarget::Session, Window::None, {}},
    {proto::LinkOp::Kind::SetWideIndex, OpTarget::Session, Window::Framed, {}},
    {proto::LinkOp::Kind::CoreReset, OpTarget::Session, Window::FramedLive, {}},
    {proto::LinkOp::Kind::LoadFacts, OpTarget::Session, Window::Framed, {}},
});
static_assert(infra::rows_are_ordinal(kLinkOpFacts),
              "kLinkOpFacts is one row per LinkOp kind, in ordinal order");

[[nodiscard]] constexpr OpTarget target_for(proto::LinkOp::Kind k) noexcept {
    const std::size_t i = infra::ordinal(k);
    return i < kLinkOpFacts.size() ? kLinkOpFacts[i].target : OpTarget::kCount;
}

[[nodiscard]] constexpr Window window_for(proto::LinkOp::Kind k) noexcept {
    const std::size_t i = infra::ordinal(k);
    return i < kLinkOpFacts.size() ? kLinkOpFacts[i].window : Window::None;
}

}  // namespace mister::app
