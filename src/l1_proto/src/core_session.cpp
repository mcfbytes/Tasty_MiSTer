// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/core_session.h"
#include "proto/spi_frame.h"
#include "proto/uio_opcodes.h"

#include <span>

namespace mister::proto {

Ex<void> CoreSession::fail(Error e) {
    abandon_configure();
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

    dual_sdram_ = id->dual_sdram();
    type_ = fold_core_type(id->type);

    caps_ = signals_->latch_capabilities();
    if (type_ == CoreType::Unknown) {
        caps_.width = hal::Width::Byte;
        caps_.io_version = hal::IoVersion{0};
    }

    phase_ = SessionPhase::Identified;
    return type_;
}

Ex<void> CoreSession::set_memory_size(MemSizeCookie cookie) {
    if (memsz_post_ != MemSizePost::None)
        return fail(Error{Errc::would_block, ERR_SITE(), 0});
    if (phase_ != SessionPhase::Identified) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 0});
    }

    if (type_ != CoreType::EightBit) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 1});
    }

    link_->select(hal::ChipSelect::Io);
    auto op = link_->transfer_or_post(word(UioOp::SetMemSz));
    if (!op) {
        link_->deselect();
        return fail(op.error());
    }
    if (!*op) {
        memsz_post_ = MemSizePost::Opcode;
        return {};
    }

    auto data = link_->transfer_or_post(hal::SpiWord{cookie.v});
    if (!data) {
        link_->deselect();
        return fail(data.error());
    }
    if (!*data) {
        memsz_post_ = MemSizePost::Cookie;
        return {};
    }
    link_->deselect();
    phase_ = SessionPhase::MemSized;
    return {};
}

void CoreSession::abandon_configure() noexcept {
    if (memsz_post_ == MemSizePost::None)
        return;
    memsz_post_ = MemSizePost::None;
    link_->abandon_post();
    link_->deselect();
}

Ex<bool> CoreSession::settle_configure() {
    if (memsz_post_ == MemSizePost::None) {
        const Error e{Errc::negotiation, ERR_SITE(), 0};
        (void)fail(e);
        return std::unexpected(e);
    }
    auto done = link_->posted_done();
    if (!done) {
        (void)fail(done.error());
        return std::unexpected(done.error());
    }

    if (!*done)
        return false;

    if (memsz_post_ == MemSizePost::Opcode) {
        memsz_post_ = MemSizePost::None;
        auto data = link_->transfer_or_post(hal::SpiWord{cookie_.v});
        if (!data) {
            link_->deselect();
            (void)fail(data.error());
            return std::unexpected(data.error());
        }
        if (!*data) {
            memsz_post_ = MemSizePost::Cookie;
            return false;
        }
    }
    memsz_post_ = MemSizePost::None;
    link_->deselect();
    phase_ = SessionPhase::MemSized;
    if (auto r = assert_reset(); !r)
        return std::unexpected(r.error());
    return true;
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
    if (auto r = apply(StatusEdit::bit(bit, true)); !r)
        return r;
    return apply(StatusEdit::bit(bit, false));
}

Ex<void> CoreSession::pulse_reset() {
    return pulse(kResetBit);
}

Ex<void> CoreSession::adopt_from_core(const StatusWord& core) {

    const StatusWord echo = StatusEdit::bit(kResetBit, false).over(core);
    status_.adopt(echo);
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
    if (auto r = frame.begin(UioOp::Rtc); !r)
        return fail(r.error());
    for (const std::uint16_t w : image.rtc_words()) {
        if (auto r = frame.word(w); !r)
            return fail(r.error());
    }
    if (auto r = frame.end(); !r)
        return fail(r.error());

    if (auto r = frame.begin(UioOp::Timestamp); !r)
        return fail(r.error());
    for (const std::uint16_t w : image.stamp_words()) {
        if (auto r = frame.word(w); !r)
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
    if (auto r = set_memory_size(cookie_); !r)
        return r;

    if (memsz_post_ != MemSizePost::None)
        return {};
    return assert_reset();
}

bool CoreSession::negotiating() const noexcept {
    return phase_ == SessionPhase::Identified
        || phase_ == SessionPhase::MemSized
        || phase_ == SessionPhase::HeldInReset
        || phase_ == SessionPhase::ConfStrRead;
}

}
