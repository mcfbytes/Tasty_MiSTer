// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/edid_store.h"
#include "svc/adv7513_reg_map.h"
#include "svc/adv7513_bus.h"
#include "svc/adv7513_reg_write.h"

#include "os/clock.h"
#include "os/delay.h"

#include <chrono>
#include <iterator>

#include "video_service_detail.h"

namespace mister::svc {

namespace {

constexpr std::uint8_t kEdidMagic[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

bool edid_header_valid(std::span<const std::byte> buf) {
    if (buf.size() < std::size(kEdidMagic))
        return false;
    for (std::size_t i = 0; i < std::size(kEdidMagic); ++i) {
        if (std::to_integer<std::uint8_t>(buf[i]) != kEdidMagic[i])
            return false;
    }
    return true;
}

}

std::optional<std::span<const std::byte>> EdidStore::current() const noexcept {
    if (segments_live_.empty())
        return std::nullopt;
    return std::span<const std::byte>{segments_[0]};
}

Ex<bool> EdidStore::read_segment(
    const adv7513::Io& io,
    std::uint8_t segment,
    std::span<std::byte> out
) {
    constexpr std::uint8_t kTriggerStep1 = 0x03;
    constexpr std::uint8_t kTriggerStep2 = 0x13;

    constexpr std::uint8_t kClearEdidReady = adv7513::kEdidReadyBit;

    if (auto r = io.main->write(adv7513::kRegIntStatus, kClearEdidReady); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    if (auto r = io.main->write(adv7513::kRegEdidSegment, segment); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    if (auto r = io.main->write(adv7513::kRegEdidTrigger, kTriggerStep1); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    io.delay->sleep_for(kTriggerSettle);
    if (auto r = io.main->write(adv7513::kRegEdidTrigger, kTriggerStep2); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }

    const auto deadline = io.clock->now() + kReadyTimeout;
    for (;;) {
        auto status = io.main->read(adv7513::kRegIntStatus);

        if (status && adv7513::IntBits{*status}.edid_ready()) {
            if (auto r = io.main->write(adv7513::kRegIntStatus, kClearEdidReady); !r) {
                return err(r.error().code, ERR_SITE(), r.error().detail);
            }

            for (std::size_t i = 0; i < kEdidSegmentBytes; ++i) {
                auto v = io.edid->read(os::I2cReg{}.plus(i));
                out[i] = v ? static_cast<std::byte>(*v) : std::byte{0};
            }

            if (auto r = io.main->write(adv7513::kRegEdidTrigger, kTriggerStep1); !r) {
                return err(r.error().code, ERR_SITE(), r.error().detail);
            }
            return true;
        }
        if (io.clock->now() >= deadline)
            break;
        io.delay->sleep_for(kReadyPoll);
    }

    ++ready_timeouts_;
    if (auto r = io.main->write(adv7513::kRegEdidTrigger, kTriggerStep1); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    return false;
}

Ex<void> EdidStore::refresh(const adv7513::Io& io) {

    if (!io.main || !io.edid || !io.delay || !io.clock) {
        return err(Errc::negotiation, ERR_SITE());
    }

    std::array<EdidSegment, kMaxEdidSegments> buf{};
    bool ddc_responded = false;
    bool ok = false;

    for (int k = 0; k < kMaxRetries; ++k) {

        auto hpd = io.main->read(adv7513::kRegStatus);
        if (!hpd)
            return err(hpd.error().code, ERR_SITE(), hpd.error().detail);
        if (!adv7513::LinkStatus{*hpd}.sink_present())
            return {};

        auto got = read_segment(io, 0, buf[0]);
        if (!got)
            return err(got.error().code, ERR_SITE(), got.error().detail);
        if (*got)
            ddc_responded = true;
        if (edid_header_valid(buf[0])) {
            ok = true;
            break;
        }

        if (!*got)
            break;
        io.delay->sleep_for(kRetryDelay);
    }

    if (!ok) {

        (void)ddc_responded;
        return {};
    }

    const EdidSegmentCount segments = EdidSegmentCount::announced_by(buf[0]);
    const std::span<const EdidSegment> live = segments.prefix_of(buf);
    for (std::size_t i = 1; i < live.size(); ++i) {
        auto got = read_segment(io, static_cast<std::uint8_t>(i), buf[i]);
        if (!got)
            return err(got.error().code, ERR_SITE(), got.error().detail);
    }

    segments_ = buf;
    segments_live_ = segments;

    ++gen_.v;
    return {};
}

std::span<const std::byte> EdidStore::full() const noexcept {
    return std::as_bytes(segments_live_.prefix_of(segments_));
}

}
