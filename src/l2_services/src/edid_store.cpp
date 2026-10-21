// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/edid_store.h"
#include "svc/adv7513_reg_map.h"
#include "svc/adv7513_reg_write.h"

#include "os/clock.h"
#include "os/delay.h"

#include <chrono>
#include <cstring>

#include "video_service_detail.h"

namespace mister::svc {

namespace {

constexpr std::uint8_t kEdidMagic[8] = {0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

std::uint32_t now_ms(const adv7513::Io& io) {
    return static_cast<std::uint32_t>(io.clock->now().count() / 1'000'000);
}

bool edid_header_valid(std::span<const std::byte> buf) {
    if (buf.size() < 8)
        return false;
    for (std::size_t i = 0; i < 8; ++i) {
        if (std::to_integer<std::uint8_t>(buf[i]) != kEdidMagic[i])
            return false;
    }
    return true;
}

}

Ex<bool> EdidStore::read_segment(std::uint8_t segment, std::span<std::byte> out) {

    if (auto r = io_.main->write(adv7513::kRegIntStatus, 4); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    if (auto r = io_.main->write(adv7513::kRegEdidSegment, segment); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    if (auto r = io_.main->write(adv7513::kRegEdidTrigger, 0x03); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    io_.delay->sleep_for(std::chrono::microseconds{kTriggerSettleUs});
    if (auto r = io_.main->write(adv7513::kRegEdidTrigger, 0x13); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }

    const std::uint32_t start = now_ms(io_);
    for (;;) {
        auto status = io_.main->read(adv7513::kRegIntStatus);

        if (status && (*status & 4u)) {
            if (auto r = io_.main->write(adv7513::kRegIntStatus, 4); !r) {
                return err(r.error().code, ERR_SITE(), r.error().detail);
            }

            for (std::uint16_t i = 0; i < 256; ++i) {
                auto v = io_.edid->read(static_cast<std::uint8_t>(i));
                out[i] = v ? static_cast<std::byte>(*v) : std::byte{0};
            }

            if (auto r = io_.main->write(adv7513::kRegEdidTrigger, 0x03); !r) {
                return err(r.error().code, ERR_SITE(), r.error().detail);
            }
            return true;
        }
        if ((now_ms(io_) - start) >= kReadyTimeoutMs)
            break;
        io_.delay->sleep_for(std::chrono::microseconds{kReadyPollUs});
    }

    ++ready_timeouts_;
    if (auto r = io_.main->write(adv7513::kRegEdidTrigger, 0x03); !r) {
        return err(r.error().code, ERR_SITE(), r.error().detail);
    }
    return false;
}

Ex<void> EdidStore::refresh() {

    if (!io_.main || !io_.edid || !io_.delay || !io_.clock) {
        return err(Errc::negotiation, ERR_SITE());
    }

    std::array<std::byte, 256 * kMaxSegments> buf{};
    bool ddc_responded = false;
    bool ok = false;

    for (int k = 0; k < kMaxRetries; ++k) {

        auto hpd = io_.main->read(adv7513::kRegStatus);
        if (!hpd)
            return err(hpd.error().code, ERR_SITE(), hpd.error().detail);
        if ((*hpd & 0x60u) != 0x60u)
            return {};

        auto got = read_segment(0, std::span<std::byte>(buf.data(), 256));
        if (!got)
            return err(got.error().code, ERR_SITE(), got.error().detail);
        if (*got)
            ddc_responded = true;
        if (edid_header_valid(std::span<const std::byte>(buf.data(), 256))) {
            ok = true;
            break;
        }

        if (!*got)
            break;
        io_.delay->sleep_for(std::chrono::microseconds{kRetryDelayUs});
    }

    if (!ok) {

        (void)ddc_responded;
        return {};
    }

    std::size_t blocks = (2u + std::to_integer<std::uint8_t>(buf[126])) / 2u;
    if (blocks > kMaxSegments)
        blocks = kMaxSegments;
    if (blocks < 1)
        blocks = 1;
    for (std::size_t i = 1; i < blocks; ++i) {
        auto got = read_segment(
            static_cast<std::uint8_t>(i),
            std::span<std::byte>(buf.data() + i * 256, 256)
        );
        if (!got)
            return err(got.error().code, ERR_SITE(), got.error().detail);
    }

    full_ = buf;
    full_len_ = blocks * 256;
    std::memcpy(edid_.bytes.data(), buf.data(), edid_.bytes.size());
    edid_.valid = true;

    ++gen_.v;
    return {};
}

std::span<const std::byte> EdidStore::full() const noexcept {
    return std::span<const std::byte>(full_.data(), full_len_);
}

}
