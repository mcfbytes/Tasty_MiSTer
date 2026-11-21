// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "app/carrier.h"
#include "app/link_encoder.h"
#include "app/session_tally.h"
#include "proto/build_generation.h"
#include "proto/link/link.h"
#include "proto/link_op.h"
#include "proto/link/spi_frame.h"
#include "infra/seat.h"

namespace mister::hal {
class ISpiTransport;
class ICoreSignals;
}

namespace mister::proto::link {
class CoreSession;
class BlockSlots;
}

namespace mister::svc::input {
class InputEmitter;
}

namespace mister::app {

class OsdWire;

class SpiEncoder final : public ILinkEncoder {
    TASTY_SEAT_RESIDENT(RT);

public:
    SpiEncoder(
        hal::ISpiTransport& link,
        hal::ICoreSignals& signals,
        proto::link::CoreSession& session,
        OsdWire& osd,
        proto::link::BlockSlots& slots,
        svc::input::InputEmitter& em,
        SessionTally& session_tally
    ) noexcept;

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
    Outcome on(const proto::LinkOp::MenuCtl& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::PulseOption& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::ApplyCheats& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::SetDip& a, const LinkOpCtx& ctx) noexcept;
    Outcome on(const proto::LinkOp::SetCoreOption& a, const LinkOpCtx& ctx) noexcept;
    Outcome misrouted(const proto::LinkOp& op, const LinkOpCtx& ctx) noexcept;

    void bind_pads(Carrier pads, proto::BuildGeneration gen) noexcept;

    void drop_pads() noexcept;
    [[nodiscard]] bool pads_relayed() const noexcept;

    [[nodiscard]] std::uint32_t link_op_misrouted() const noexcept { return link_op_misrouted_; }

private:
    std::uint32_t holds_ = 0;
    hal::ISpiTransport& link_;

    proto::link::SpiFrame frame_{link_};
    hal::ICoreSignals& signals_;
    proto::link::CoreSession& session_;
    OsdWire& osd_;
    proto::link::BlockSlots& slots_;
    svc::input::InputEmitter& em_;

    SessionTally& session_tally_;
    std::uint32_t link_op_misrouted_ = 0;
};

}
