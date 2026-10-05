// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <concepts>
#include <utility>

#include "app/link_encoder.h"
#include "app/link_op_ctx.h"
#include "app/link_op_facts.h"
#include "infra/message_sum.h"
#include "proto/link_op.h"

namespace mister::app {

template <OpTarget T>
struct LinkOpRoutes {
    template <class A>
    static constexpr bool routed = kLinkOpFacts[infra::ordinal(A::kKind)].target == T;
};

template <class S>
concept LinkOpWireSink = std::same_as<typename S::Result, ILinkEncoder::Outcome> &&
                         requires(S& s, const proto::LinkOp& m, const LinkOpCtx& c) {
                             {
                                 s.on(std::declval<const proto::LinkOp::HoldReset&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             {
                                 s.on(std::declval<const proto::LinkOp::ReleaseReset&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             {
                                 s.on(std::declval<const proto::LinkOp::WriteStatus&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             {
                                 s.on(std::declval<const proto::LinkOp::OsdFlush&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             {
                                 s.on(std::declval<const proto::LinkOp::JoyEmit&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             {
                                 s.on(std::declval<const proto::LinkOp::SendRtc&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             {
                                 s.on(std::declval<const proto::LinkOp::BlockAnswer&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             {
                                 s.on(std::declval<const proto::LinkOp::BindJoy&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             {
                                 s.on(std::declval<const proto::LinkOp::JoyRelease&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             {
                                 s.on(std::declval<const proto::LinkOp::SaveUpload&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             {
                                 s.on(std::declval<const proto::LinkOp::Ps2Frame&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             {
                                 s.on(std::declval<const proto::LinkOp::SetOsdVisible&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             {
                                 s.on(std::declval<const proto::LinkOp::PulseOption&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             {
                                 s.on(std::declval<const proto::LinkOp::SetVideoMode&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             {
                                 s.on(std::declval<const proto::LinkOp::ApplyCheats&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             {
                                 s.on(std::declval<const proto::LinkOp::SetDip&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             {
                                 s.on(std::declval<const proto::LinkOp::SetCoreOption&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             {
                                 s.on(std::declval<const proto::LinkOp::SettleCoreOptions&>(), c)
                             } -> std::same_as<ILinkEncoder::Outcome>;
                             { s.misrouted(m, c) } -> std::same_as<ILinkEncoder::Outcome>;
                         };

template <class S>
concept LinkOpSessionSink = std::same_as<typename S::Result, ILinkEncoder::Outcome> &&
                            requires(S& s, const proto::LinkOp& m, const LinkOpCtx& c) {
                                {
                                    s.on(std::declval<const proto::LinkOp::BindDecoders&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::BindSlot&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::FileTx&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::BindFacts&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::BindConfig&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::BindUart&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::BindSlotConfig&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::BindMount&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::BindSlotPreviews&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::RebootNow&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::ApplyCore&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::AbortSwitch&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::BindIdentity&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::BindDoorbells&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::SaveAsk&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::DropCore&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::MakeCore&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::SessionUp&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::StageMount&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::StagePayload&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::StageReset&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::StageDiscPayload&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::StageCheats&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::SetVolume&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::AnnounceMount&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::SetWideIndex&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::CoreReset&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::LoadFacts&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                {
                                    s.on(std::declval<const proto::LinkOp::MailboxWrite&>(), c)
                                } -> std::same_as<ILinkEncoder::Outcome>;
                                { s.misrouted(m, c) } -> std::same_as<ILinkEncoder::Outcome>;
                            };

[[nodiscard]] constexpr bool admitted(Window w, const LinkOpCtx& c) noexcept {
    switch (w) {
        case Window::None:
            return true;
        case Window::Framed:
            return !c.windows_closed;
        case Window::FramedLive:
            return !c.windows_closed && c.live;
    }
    return false;
}

}  // namespace mister::app
