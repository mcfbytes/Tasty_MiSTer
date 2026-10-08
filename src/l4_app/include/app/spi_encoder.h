// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/link_encoder.h"
#include "proto/link_op.h"
#include "infra/seat.h"

namespace mister::hal {
class ISpiTransport;
class ICoreSignals;
}  // namespace mister::hal

namespace mister::proto {
class CoreSession;
class BlockSlots;
}  // namespace mister::proto

namespace mister::svc {
class InputEmitter;
}

namespace mister::app {

class OsdWire;
class VideoWire;

class SpiEncoder final : public ILinkEncoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    SpiEncoder(hal::ISpiTransport& link, hal::ICoreSignals& signals, proto::CoreSession& session,
               OsdWire& osd, proto::BlockSlots& slots, VideoWire& video,
               svc::InputEmitter& em) noexcept;

    using Result = ILinkEncoder::Outcome;

    Outcome encode(const proto::LinkOp& op, const LinkOpCtx& ctx) noexcept override;

    Outcome on(const proto::LinkOp::HoldReset& a, const LinkOpCtx& ctx) noexcept;

    [[nodiscard]] std::uint32_t holds() const noexcept { return holds_; }
    Outcome on(const proto::LinkOp::ReleaseReset& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::WriteStatus& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::OsdFlush& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::JoyEmit& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::SendRtc& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::BlockAnswer& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::BindJoy& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::JoyRelease& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::SaveUpload& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::Ps2Frame& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::SetOsdVisible& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::PulseOption& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::SetVideoMode& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::ApplyCheats& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::SetDip& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::SetCoreOption& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::SettleCoreOptions& a, const LinkOpCtx& ctx) noexcept;
    Outcome misrouted(const proto::LinkOp& op, const LinkOpCtx& ctx) noexcept;
    [[nodiscard]] std::uint32_t link_op_misrouted() const noexcept { return link_op_misrouted_; }

private:
    std::uint32_t holds_ = 0;
    hal::ISpiTransport& link_;
    hal::ICoreSignals& signals_;
    proto::CoreSession& session_;
    OsdWire& osd_;
    proto::BlockSlots& slots_;
    VideoWire& video_;
    svc::InputEmitter& em_;
    std::uint32_t link_op_misrouted_ = 0;
};

}  // namespace mister::app
