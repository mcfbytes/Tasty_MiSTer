// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/spi_encoder.h"

#include <cstdint>
#include <span>

#include "app/encoder_roles.h"
#include "app/link_op_dispatch.h"
#include "app/link_tx_channel.h"
#include "app/osd_wire.h"
#include "app/video_wire.h"
#include "hal/reg_block.h"
#include "app/save_flush.h"
#include "app/cheat_apply.h"
#include "app/core_option_acts.h"
#include "hal/core_signals.h"
#include "hal/spi_transport.h"
#include "proto/block_slots.h"
#include "proto/core_session.h"
#include "proto/joystick.h"
#include "proto/link_op.h"
#include "proto/osd_surface.h"
#include "proto/ps2_frame.h"
#include "proto/ps2_wire.h"
#include "proto/osd_transport.h"
#include "proto/status_register.h"
#include "svc/input_emitter.h"

namespace mister::app {

SpiEncoder::SpiEncoder(
    hal::ISpiTransport& link,
    hal::ICoreSignals& signals,
    proto::CoreSession& session,
    OsdWire& osd,
    proto::BlockSlots& slots,
    VideoWire& video,
    svc::InputEmitter& em
) noexcept
    : link_(link),
      signals_(signals),
      session_(session),
      osd_(osd),
      slots_(slots),
      video_(video),
      em_(em) {}

ILinkEncoder::Outcome SpiEncoder::encode(const proto::LinkOp& op, const LinkOpCtx& ctx) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    return infra::dispatch<LinkOpRoutes<OpTarget::Wire>>(op, *this, ctx);
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::HoldReset&, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    signals_.set_core_reset(true);
    ++holds_;
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(
    const proto::LinkOp::ReleaseReset&,
    const LinkOpCtx&
) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    signals_.set_core_reset(false);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(
    const proto::LinkOp::WriteStatus& a,
    const LinkOpCtx&
) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if (session_.type() != proto::CoreType::EightBit) {
        return ILinkEncoder::Outcome::Dropped;
    }
    const std::uint32_t width = a.width == 0 ? 1u : a.width;
    if (static_cast<std::uint32_t>(a.start.v) + width > proto::StatusRegister::kBits) {
        return ILinkEncoder::Outcome::Dropped;
    }
    if (auto r = session_.apply(proto::StatusEdit::field(a.start, width, a.value)); !r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::EncodedStatusChange;
}

ILinkEncoder::Outcome SpiEncoder::on(
    const proto::LinkOp::OsdFlush& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if ((a.rows > 1) || a.row.v >= proto::OsdSurface::kMaxRows) {
        return ILinkEncoder::Outcome::Dropped;
    }
    proto::OsdTransport osd;
    const proto::OsdRow row = a.row;
    Ex<void> flushed;
    if (a.bytes == proto::TxSlabId{}) {
        proto::OsdSurface::Row sampled;
        if (osd_.surface().sample(row, sampled) == 0) {
            return ILinkEncoder::Outcome::Dropped;
        }
        flushed = osd.flush_row(
            link_,
            row,
            std::span<const std::uint8_t>(sampled.b, sizeof sampled.b)
        );
    } else {
        const auto bytes = ctx.inbox.bytes(a.bytes);
        flushed = osd.flush_row(link_, row, bytes);
    }
    if (!flushed) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::JoyEmit& a, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if (a.player.v >= proto::JoystickPort::kMaxPorts) {
        return ILinkEncoder::Outcome::Dropped;
    }
    const auto r = em_.submit(frame_, a.player, a.mask, a.autofire);
    if (!r || !*r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::BindJoy& a, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    proto::JoystickWire jw{};
    jw.joyswap = a.joyswap;
    jw.suppress_analog_followup = a.suppress_analog_followup;
    jw.joy_transl = a.joy_transl;

    jw.analog_capable = session_.type() == proto::CoreType::EightBit;
    jw.analog_word = session_.capabilities().io_version != 0;
    em_.joysticks().configure(jw);
    return ILinkEncoder::Outcome::Encoded;
}

void SpiEncoder::bind_pads(infra::OptRef<const LwPads> granted) noexcept {
    if (granted) {

        em_.bind_pads(frame_, hal::reg::Block::redeem(granted->regs.lend()), granted->block);
    } else {
        em_.bind_pads(frame_);
    }
}

void SpiEncoder::drop_pads() noexcept {
    em_.hold_pads();
}

void SpiEncoder::trust_pads(bool ready) noexcept {
    em_.trust_pads(ready);
}

bool SpiEncoder::pads_on_lw() const noexcept {
    return em_.pads_on_lw();
}

ILinkEncoder::Outcome SpiEncoder::on(
    const proto::LinkOp::JoyRelease& a,
    const LinkOpCtx&
) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if (a.player.v >= proto::JoystickPort::kMaxPorts) {
        return ILinkEncoder::Outcome::Dropped;
    }
    const auto r = em_.release(frame_, a.player, a.mask);
    if (!r || !*r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::Ps2Frame& a, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    const proto::Ps2Frame& frame = a.frame;
    if (frame.len == 0 || !proto::write_ps2_frame(link_, frame)) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::SendRtc& a, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    const auto r = session_.send_rtc_image(a.image);
    if (!r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(
    const proto::LinkOp::BlockAnswer& a,
    const LinkOpCtx&
) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if (a.slot.v >= proto::kBlockSlots) {
        return ILinkEncoder::Outcome::Dropped;
    }
    const auto r = slots_.answer_read(link_, a.slot, a.lba, a.bytes, a.ack);
    if (!r || !*r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(
    const proto::LinkOp::SaveUpload&,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if (!ctx.roles || !ctx.roles->saves) {
        return ILinkEncoder::Outcome::Dropped;
    }
    if (!ctx.roles->saves->save_upload_counted()) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(
    const proto::LinkOp::SetVideoMode& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);

    if (!video_.emit(link_, a.block, a.gen, infra::OptRef<proto::IResetFence>{ctx.fence})) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(
    const proto::LinkOp::PulseOption& a,
    const LinkOpCtx&
) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);

    if (session_.type() != proto::CoreType::EightBit) {
        return ILinkEncoder::Outcome::Dropped;
    }
    if (a.bit.v >= proto::StatusRegister::kBits) {
        return ILinkEncoder::Outcome::Dropped;
    }
    if (auto r = session_.pulse(a.bit); !r)
        return ILinkEncoder::Outcome::Dropped;
    return ILinkEncoder::Outcome::EncodedStatusChange;
}

ILinkEncoder::Outcome SpiEncoder::on(
    const proto::LinkOp::ApplyCheats&,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);

    if (!ctx.roles || !ctx.roles->cheats) {
        return ILinkEncoder::Outcome::Dropped;
    }
    if (!ctx.roles->cheats->apply_cheats_counted(ctx.roles->cheat_call)) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(
    const proto::LinkOp::SetDip& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);

    if (!ctx.roles || !ctx.roles->acts) {
        return ILinkEncoder::Outcome::Dropped;
    }
    if (!ctx.roles->acts->set_dip_counted(ctx.core, a.row, a.choice)) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(
    const proto::LinkOp::SetCoreOption& a,
    const LinkOpCtx& ctx
) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if (!ctx.roles || !ctx.roles->acts) {
        return ILinkEncoder::Outcome::Dropped;
    }
    const bool ok = ctx.roles->acts->set_option_counted(ctx.core, a.row, a.choice);
    return ok ? ILinkEncoder::Outcome::Encoded : ILinkEncoder::Outcome::Dropped;
}

ILinkEncoder::Outcome SpiEncoder::on(
    const proto::LinkOp::SetOsdVisible& a,
    const LinkOpCtx&
) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);

    if (!osd_.set_show(a.show, a.target)) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::misrouted(const proto::LinkOp&, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    ++link_op_misrouted_;
    return ILinkEncoder::Outcome::Dropped;
}

static_assert(LinkOpWireSink<SpiEncoder>);

}
