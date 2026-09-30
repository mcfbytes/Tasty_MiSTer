// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/spi_encoder.h"

#include <cstdint>
#include <span>

#include "app/link_op_dispatch.h"
#include "app/link_tx_channel.h"
#include "app/osd_wire.h"
#include "app/video_wire.h"
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

SpiEncoder::SpiEncoder(hal::ISpiTransport& link, hal::ICoreSignals& signals,
                       proto::CoreSession& session, OsdWire& osd) noexcept
    : link_(&link), signals_(&signals), session_(&session), osd_(&osd) {}

ILinkEncoder::Outcome SpiEncoder::encode(const proto::LinkOp& op, const LinkOpCtx& ctx) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    return infra::dispatch<LinkOpRoutes<OpTarget::Wire>>(op, *this, ctx);
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::HoldReset&, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    signals_->set_core_reset(true);
    ++holds_;
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::ReleaseReset&,
                                     const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    signals_->set_core_reset(false);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::WriteStatus& a,
                                     const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if (link_ == nullptr) {
        return ILinkEncoder::Outcome::Dropped;
    }
    if (session_->type() != proto::CoreType::EightBit) {
        return ILinkEncoder::Outcome::Dropped;
    }
    const std::uint32_t width = a.width == 0 ? 1u : a.width;
    if (static_cast<std::uint32_t>(a.start.v) + width > proto::StatusRegister::kBits) {
        return ILinkEncoder::Outcome::Dropped;
    }
    auto& st = session_->status();
    for (std::uint32_t i = 0; i < width; ++i) {
        st.set_bit(proto::StatusBit{static_cast<std::uint8_t>(a.start.v + i)},
                   ((a.value >> i) & 1u) != 0);
    }
    if (auto r = st.flush(*link_); !r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::EncodedStatusChange;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::OsdFlush& a,
                                     const LinkOpCtx& ctx) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if (link_ == nullptr || (a.rows > 1) || a.row.v >= proto::OsdSurface::kMaxRows) {
        return ILinkEncoder::Outcome::Dropped;
    }
    proto::OsdTransport osd;
    const proto::OsdRow row = a.row;
    Ex<void> flushed;
    if (a.bytes == proto::TxSlabId{}) {
        proto::OsdSurface::Row sampled;
        if (osd_->surface().sample(row, sampled) == 0) {
            return ILinkEncoder::Outcome::Dropped;
        }
        flushed =
            osd.flush_row(*link_, row, std::span<const std::uint8_t>(sampled.b, sizeof sampled.b));
    } else if (ctx.inbox == nullptr) {
        return ILinkEncoder::Outcome::Dropped;
    } else {
        const auto bytes = ctx.inbox->bytes(a.bytes);
        flushed = osd.flush_row(*link_, row, bytes);
    }
    if (!flushed) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::JoyEmit& a, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if (link_ == nullptr || em_ == nullptr || a.player.v >= proto::JoystickPort::kMaxPorts) {
        return ILinkEncoder::Outcome::Dropped;
    }
    const auto r = em_->joysticks().submit(*link_, a.player, a.mask, a.autofire);
    if (!r || !*r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::BindJoy& a, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if (em_ == nullptr) {
        return ILinkEncoder::Outcome::Dropped;
    }
    proto::JoystickWire jw{};
    jw.joyswap = a.joyswap;
    jw.suppress_analog_followup = a.suppress_analog_followup;
    jw.joy_transl = a.joy_transl;

    jw.analog_capable = session_->type() == proto::CoreType::EightBit;
    jw.analog_word = session_->capabilities().io_version != 0;
    em_->joysticks().configure(jw);
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::JoyRelease& a,
                                     const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if (link_ == nullptr || em_ == nullptr || a.player.v >= proto::JoystickPort::kMaxPorts) {
        return ILinkEncoder::Outcome::Dropped;
    }
    const auto r = em_->joysticks().release(*link_, a.player, a.mask);
    if (!r || !*r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::Ps2Frame& a, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if (link_ == nullptr) {
        return ILinkEncoder::Outcome::Dropped;
    }
    const proto::Ps2Frame& frame = a.frame;
    if (frame.len == 0 || !proto::write_ps2_frame(*link_, frame)) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::SendRtc& a, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if (link_ == nullptr) {
        return ILinkEncoder::Outcome::Dropped;
    }
    const auto r =
        a.unix_seconds == 0 ? session_->send_rtc() : session_->send_rtc_at(a.unix_seconds);
    if (!r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::BlockAnswer& a,
                                     const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if (link_ == nullptr || slots_ == nullptr || a.slot.v >= proto::kBlockSlots) {
        return ILinkEncoder::Outcome::Dropped;
    }
    const auto r = slots_->answer_read(*link_, a.slot, a.lba, a.bytes, a.ack);
    if (!r || !*r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::SaveUpload&, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if (saves_ == nullptr) {
        return ILinkEncoder::Outcome::Dropped;
    }
    if (!saves_->save_upload_counted()) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::SetVideoMode& a,
                                     const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);

    if (link_ == nullptr || video_ == nullptr) {
        return ILinkEncoder::Outcome::Dropped;
    }
    if (!video_->emit(*link_, a.block, a.gen)) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::PulseOption& a,
                                     const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);

    if (link_ == nullptr) {
        return ILinkEncoder::Outcome::Dropped;
    }
    if (session_->type() != proto::CoreType::EightBit) {
        return ILinkEncoder::Outcome::Dropped;
    }
    if (a.bit.v >= proto::StatusRegister::kBits) {
        return ILinkEncoder::Outcome::Dropped;
    }
    auto& st = session_->status();
    st.set_bit(a.bit, true);
    if (auto r = st.flush(*link_); !r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    st.set_bit(a.bit, false);
    if (auto r = st.flush(*link_); !r) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::EncodedStatusChange;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::ApplyCheats&, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);

    if (cheats_ == nullptr) {
        return ILinkEncoder::Outcome::Dropped;
    }
    if (!cheats_->apply_cheats_counted()) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::SetDip& a, const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);

    if (acts_ == nullptr) {
        return ILinkEncoder::Outcome::Dropped;
    }
    if (!acts_->set_dip_counted(a.row, a.choice)) {
        return ILinkEncoder::Outcome::Dropped;
    }
    return ILinkEncoder::Outcome::Encoded;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::SetCoreOption& a,
                                     const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if (acts_ == nullptr) {
        return ILinkEncoder::Outcome::Dropped;
    }
    const bool ok = acts_->set_option_counted(a.row, a.choice);
    return ok ? ILinkEncoder::Outcome::Encoded : ILinkEncoder::Outcome::Dropped;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::SettleCoreOptions&,
                                     const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);
    if (acts_ == nullptr) {
        return ILinkEncoder::Outcome::Dropped;
    }
    const bool ok = acts_->settle_options_counted();
    return ok ? ILinkEncoder::Outcome::Encoded : ILinkEncoder::Outcome::Dropped;
}

ILinkEncoder::Outcome SpiEncoder::on(const proto::LinkOp::SetOsdVisible& a,
                                     const LinkOpCtx&) noexcept {
    TASTY_SEAT_BODY(SpiEncoder);

    if (osd_ == nullptr) {
        return ILinkEncoder::Outcome::Dropped;
    }
    if (!osd_->set_show(a.show)) {
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

}  // namespace mister::app
