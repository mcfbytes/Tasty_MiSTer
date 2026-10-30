// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/core_session.h"
#include "hal/start_ack_bound.h"
#include "proto/spi_frame.h"
#include "proto/uio_opcodes.h"

#include <string_view>

namespace mister::proto {

namespace {

constexpr StatusBit kResetBit{0};
}

Ex<void> CoreSession::fail(Error e) {
    phase_ = SessionPhase::Failed;
    return std::unexpected(e);
}

Ex<CoreType> CoreSession::accept_identity(const Ex<hal::CoreIdentity>& id) {

    if (phase_ != SessionPhase::Start) {
        (void)fail(Error{Errc::negotiation, ERR_SITE(), 0});
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }

    if (!id) {
        (void)fail(id.error());
        return std::unexpected(id.error());
    }

    dual_sdram_ = id->dual_sdram || id->type_byte == 0xA8;
    type_ = fold_core_type(id->type_byte);

    caps_ = signals_->latch_capabilities();
    if (type_ == CoreType::Unknown) {
        caps_.width = hal::Width::Byte;
        caps_.io_version = 0;
    }

    phase_ = SessionPhase::Identified;
    return type_;
}

Ex<void> CoreSession::set_memory_size(MemSizeCookie cookie) {
    if (phase_ != SessionPhase::Identified) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 0});
    }

    if (type_ != CoreType::EightBit) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 1});
    }
    SpiFrame frame{*link_};
    if (auto r = frame.begin(FrameSel::Uio, static_cast<std::uint16_t>(UioOp::SetMemSz)); !r)
        return fail(r.error());

    if (auto r = frame.word(cookie.v); !r)
        return fail(r.error());
    if (auto r = frame.end(); !r)
        return fail(r.error());
    phase_ = SessionPhase::MemSized;
    return {};
}

Ex<void> CoreSession::flush_status() {
    if (suppress_status_wire_)
        return {};
    SpiFrame frame{*link_};
    return status_.flush(frame);
}

void CoreSession::stage(const StatusEdit& edit) noexcept {
    status_.write(edit);
}

Ex<void> CoreSession::apply(const StatusEdit& edit) {
    status_.write(edit);
    return flush_status();
}

void CoreSession::post(const StatusEdit& edit) noexcept {
    if (!apply(edit))
        ++deferred_bursts_;
}

Ex<void> CoreSession::pulse(StatusBit bit) {
    if (bit.v >= StatusRegister::kBits) {
        return std::unexpected(Error{Errc::slot_range, ERR_SITE(), bit.v});
    }
    if (auto r = apply(StatusEdit::bit(bit, true)); !r)
        return r;
    return apply(StatusEdit::bit(bit, false));
}

Ex<void> CoreSession::pulse_reset() {
    return pulse(kResetBit);
}

Ex<void> CoreSession::adopt_from_core(const StatusWord& core) {

    const StatusWord echo = StatusEdit::bit(kResetBit, false).over(core);
    status_.adopt(echo.words);
    return flush_status();
}

Ex<void> CoreSession::flush_owed() {
    if (!status_.dirty())
        return {};
    return flush_status();
}

Ex<void> CoreSession::assert_reset() {

    if (phase_ != SessionPhase::MemSized) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    if (auto r = apply(StatusEdit::bit(kResetBit, true)); !r)
        return fail(r.error());
    phase_ = SessionPhase::HeldInReset;
    return {};
}

bool CoreSession::ladder_window_() const noexcept {
    return phase_ == SessionPhase::HeldInReset
        || (phase_ == SessionPhase::Identified && type_ != CoreType::EightBit);
}

bool CoreSession::conf_str_window_open() const noexcept {
    return ladder_window_();
}

Ex<void> CoreSession::accept_conf_str(const Ex<std::span<const std::uint8_t>>& body) {

    const bool ladder = ladder_window_();
    const bool reread = phase_ == SessionPhase::ConfStrRead || phase_ == SessionPhase::Running;
    if (!ladder && !reread) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    if (!body)
        return fail(body.error());

    if (!reread)
        phase_ = SessionPhase::ConfStrRead;
    return {};
}

Ex<void> CoreSession::load_saved_config() {

    if (phase_ != SessionPhase::ConfStrRead) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 0});
    }

    if (type_ != CoreType::EightBit) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 1});
    }

    if (auto r = apply(StatusEdit::bit(kResetBit, true)); !r)
        return fail(r.error());
    return {};
}

Ex<void> CoreSession::send_rtc_image(const RtcImage& image) {

    if (phase_ != SessionPhase::ConfStrRead && phase_ != SessionPhase::Running) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    if (type_ != CoreType::EightBit) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 1});
    }

    SpiFrame frame{*link_};
    if (auto r = frame.begin(FrameSel::Uio, static_cast<std::uint16_t>(UioOp::Rtc)); !r)
        return fail(r.error());
    for (unsigned i = 0; i < 8; i += 2) {
        const auto w = static_cast<std::uint16_t>((image.rtc[i + 1] << 8) | image.rtc[i]);
        if (auto r = frame.word(w); !r)
            return fail(r.error());
    }
    if (auto r = frame.end(); !r)
        return fail(r.error());

    if (auto r = frame.begin(FrameSel::Uio, static_cast<std::uint16_t>(UioOp::Timestamp)); !r)
        return fail(r.error());
    if (auto r = frame.word(static_cast<std::uint16_t>(image.stamp & 0xFFFFu)); !r) {
        return fail(r.error());
    }
    if (auto r = frame.word(static_cast<std::uint16_t>(image.stamp >> 16)); !r) {
        return fail(r.error());
    }
    if (auto r = frame.end(); !r)
        return fail(r.error());
    return {};
}

Ex<void> CoreSession::release_reset() {
    if (phase_ != SessionPhase::ConfStrRead) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 0});
    }

    if (type_ == CoreType::Unknown) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 1});
    }
    stage(StatusEdit::bit(kResetBit, false));

    if (type_ == CoreType::EightBit) {
        if (auto r = flush_status(); !r) {

            stage(StatusEdit::bit(kResetBit, true));
            return fail(r.error());
        }
    }
    phase_ = SessionPhase::Running;
    return {};
}

Ex<void> CoreSession::configure_after_identity() {
    if (phase_ != SessionPhase::Identified)
        return fail(Error{Errc::negotiation, ERR_SITE(), 0});

    if (type_ != CoreType::EightBit)
        return {};

    hal::StartAckBound widened(*link_);
    if (auto r = set_memory_size(cookie_); !r)
        return r;
    return assert_reset();
}

bool CoreSession::negotiating() const noexcept {
    return phase_ == SessionPhase::Identified
        || phase_ == SessionPhase::MemSized
        || phase_ == SessionPhase::HeldInReset
        || phase_ == SessionPhase::ConfStrRead;
}

}
