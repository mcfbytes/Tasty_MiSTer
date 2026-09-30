// SPDX-License-Identifier: GPL-3.0-or-later
#include "proto/core_session.h"
#include "hal/selected.h"

#include <cerrno>
#include <ctime>
#include <string_view>

namespace mister::proto {

namespace {

constexpr hal::SpiWord kSetMemSz{0x31};
constexpr hal::SpiWord kRtc{0x22};
constexpr hal::SpiWord kTimestamp{0x24};

constexpr StatusBit kResetBit{0};
}  // namespace

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

    dual_sdram_ = id->dual_sdram;
    switch (id->type_byte) {
        case 0xA4:
            type_ = CoreType::EightBit;
            break;
        case 0xA8:
            type_ = CoreType::EightBit;
            dual_sdram_ = true;
            break;
        case 0xA7:
            type_ = CoreType::SharpMz;
            break;
        default:
            type_ = CoreType::Unknown;
            break;
    }

    caps_ = signals_->capabilities();
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
    hal::Selected cs(*link_, hal::ChipSelect::Io);
    if (auto r = link_->transfer(kSetMemSz); !r) return fail(r.error());

    if (auto r = link_->transfer(hal::SpiWord{cookie.v}); !r) {
        return fail(r.error());
    }
    phase_ = SessionPhase::MemSized;
    return {};
}

Ex<void> CoreSession::flush_status() {
    if (suppress_status_wire_) return {};
    return status_.flush(*link_);
}

Ex<void> CoreSession::assert_reset() {

    if (phase_ != SessionPhase::MemSized) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    status_.set_bit(kResetBit, true);
    if (auto r = flush_status(); !r) return fail(r.error());
    phase_ = SessionPhase::HeldInReset;
    return {};
}

bool CoreSession::ladder_window_() const noexcept {
    return phase_ == SessionPhase::HeldInReset ||
           (phase_ == SessionPhase::Identified && type_ != CoreType::EightBit);
}

bool CoreSession::conf_str_window_open() const noexcept { return ladder_window_(); }

Ex<void> CoreSession::accept_conf_str(const Ex<std::span<const std::uint8_t>>& body) {

    const bool ladder = ladder_window_();
    const bool reread = phase_ == SessionPhase::ConfStrRead || phase_ == SessionPhase::Running;
    if (!ladder && !reread) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    if (!body) return fail(body.error());

    if (!reread) phase_ = SessionPhase::ConfStrRead;
    return {};
}

Ex<void> CoreSession::load_saved_config() {

    if (phase_ != SessionPhase::ConfStrRead) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 0});
    }

    if (type_ != CoreType::EightBit) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 1});
    }

    status_.set_bit(kResetBit, true);
    if (auto r = flush_status(); !r) return fail(r.error());
    return {};
}

Ex<void> CoreSession::send_rtc() {
    const std::time_t t = std::time(nullptr);
    if (t == static_cast<std::time_t>(-1)) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    return send_rtc_at(static_cast<std::uint32_t>(static_cast<std::uint64_t>(t)));
}

Ex<void> CoreSession::send_rtc_at(std::uint32_t unix_seconds) {

    if (phase_ != SessionPhase::ConfStrRead && phase_ != SessionPhase::Running) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    if (type_ != CoreType::EightBit) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 1});
    }

    const std::time_t t = static_cast<std::time_t>(unix_seconds);
    std::tm local{};
    if (::localtime_r(&t, &local) == nullptr) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }

    std::tm utc{};
    if (::gmtime_r(&t, &utc) == nullptr) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    const std::time_t utc_as_local = std::mktime(&utc);
    if (utc_as_local == static_cast<std::time_t>(-1)) {
        return std::unexpected(Error{Errc::os, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    const std::int64_t shifted =
        static_cast<std::int64_t>(t) +
        (static_cast<std::int64_t>(t) - static_cast<std::int64_t>(utc_as_local));
    const auto stamp = static_cast<std::uint32_t>(static_cast<std::uint64_t>(shifted));

    const auto bcd = [](int v) { return static_cast<std::uint8_t>((v % 10) | ((v / 10) << 4)); };
    std::uint8_t rtc[8];
    rtc[0] = bcd(local.tm_sec);
    rtc[1] = bcd(local.tm_min);
    rtc[2] = bcd(local.tm_hour);
    rtc[3] = bcd(local.tm_mday);
    rtc[4] = bcd(local.tm_mon + 1);
    rtc[5] = static_cast<std::uint8_t>((local.tm_year % 10) | (((local.tm_year / 10) % 10) << 4));
    rtc[6] = static_cast<std::uint8_t>(local.tm_wday);
    rtc[7] = 0x40;
    {
        hal::Selected cs(*link_, hal::ChipSelect::Io);
        if (auto r = link_->transfer(kRtc); !r) return fail(r.error());
        for (unsigned i = 0; i < 8; i += 2) {
            const auto w = static_cast<std::uint16_t>((rtc[i + 1] << 8) | rtc[i]);
            if (auto r = link_->transfer(hal::SpiWord{w}); !r) {
                return fail(r.error());
            }
        }
    }

    {
        hal::Selected cs(*link_, hal::ChipSelect::Io);
        if (auto r = link_->transfer(kTimestamp); !r) return fail(r.error());
        if (auto r = link_->transfer(hal::SpiWord{static_cast<std::uint16_t>(stamp & 0xFFFFu)});
            !r) {
            return fail(r.error());
        }
        if (auto r = link_->transfer(hal::SpiWord{static_cast<std::uint16_t>(stamp >> 16)}); !r) {
            return fail(r.error());
        }
    }
    return {};
}

Ex<void> CoreSession::release_reset() {
    if (phase_ != SessionPhase::ConfStrRead) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 0});
    }

    if (type_ == CoreType::Unknown) {
        return fail(Error{Errc::negotiation, ERR_SITE(), 1});
    }
    status_.set_bit(kResetBit, false);

    if (type_ == CoreType::EightBit) {
        if (auto r = flush_status(); !r) {

            status_.set_bit(kResetBit, true);
            return fail(r.error());
        }
    }
    phase_ = SessionPhase::Running;
    return {};
}

Ex<void> CoreSession::configure_after_identity() {
    if (phase_ != SessionPhase::Identified) return fail(Error{Errc::negotiation, ERR_SITE(), 0});

    if (type_ != CoreType::EightBit) return {};
    if (auto r = set_memory_size(cookie_); !r) return r;
    return assert_reset();
}

bool CoreSession::negotiating() const noexcept {
    return phase_ == SessionPhase::Identified || phase_ == SessionPhase::MemSized ||
           phase_ == SessionPhase::HeldInReset || phase_ == SessionPhase::ConfStrRead;
}

}  // namespace mister::proto
