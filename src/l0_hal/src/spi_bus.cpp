// SPDX-License-Identifier: GPL-3.0-or-later
#include "hal/spi_bus.h"

#include <ctime>
#include <new>
#include <type_traits>
#include <utility>

#include "infra/rt_stats.h"
#include "hal/mailbox.h"

namespace mister::hal {

namespace {

constexpr std::uint32_t kData = 0x0000FFFFu;
constexpr std::uint32_t kStrobe = 1u << 17;
constexpr std::uint32_t kFpgaEnable = 1u << 18;
constexpr std::uint32_t kOsdEnable = 1u << 19;
constexpr std::uint32_t kIoEnable = 1u << 20;
constexpr std::uint32_t kDiskLed = 1u << 29;
constexpr std::uint32_t kCoreReset = 1u << 30;
constexpr std::uint32_t kCoreEnable = 1u << 31;
constexpr std::uint32_t kCsMask = kFpgaEnable | kOsdEnable | kIoEnable;

constexpr std::uint32_t kAck = kStrobe;
constexpr std::uint32_t kNotReady = 1u << 31;

constexpr std::uint8_t kCoreTypeDualSdram = 0xA8;

std::uint64_t now_ns() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

}  // namespace

void SpiBus::store_gpo(std::uint32_t v) noexcept {
    *gpo_reg_ = v;
    if (obs_ != nullptr) obs_->on_store(v);
}

void SpiBus::commit_gpo(std::uint32_t v) noexcept {
    gpo_.v = v;
    store_gpo(v);
}

std::uint32_t SpiBus::read_gpi() const noexcept { return *gpi_reg_; }

Ex<std::uint32_t> SpiBus::spin_ack(bool want) noexcept {
    unsigned budget = ack_soft_spins_;
    std::uint64_t limit = 0;
    bool armed = false;
    for (;;) {
        const std::uint32_t gpi = read_gpi();
        if ((gpi & kNotReady) != 0u) {

            return std::unexpected(Error{Errc::fpga_not_ready, ERR_SITE(), gpi});
        }
        if (((gpi & kAck) != 0u) == want) return gpi;
        if (--budget != 0u) continue;
        budget = ack_soft_spins_;
        const std::uint64_t now = now_ns();
        if (!armed) {
            limit = now + ack_timeout_ns_;
            armed = true;
            continue;
        }
        if (now >= limit) {
            if (stats_ != nullptr) stats_->spin_timeouts.add();
            return std::unexpected(Error{Errc::spi_timeout, ERR_SITE(), gpi});
        }
    }
}

Ex<SpiBus> SpiBus::open(Mailbox mailbox, xthread::RtStats* stats) {

    if (!mailbox.held()) return std::unexpected(Error{Errc::busy, ERR_SITE(), 0});
    const LinkTimingValues& timing = mailbox.timing();

    SpiBus bus;
    bus.gpo_reg_ = mailbox.gpo();
    bus.gpi_reg_ = mailbox.gpi();
    bus.stats_ = stats;
    bus.ack_soft_spins_ = timing.ack_soft_spins;
    bus.ack_timeout_ns_ = timing.ack_timeout_ns;

    bus.mailbox_ = std::move(mailbox).consume();

    bus.commit_gpo(0);
    return bus;
}

SpiBus SpiBus::attach_registers(volatile std::uint32_t* gpo, const volatile std::uint32_t* gpi,
                                ISpiOutputObserver* obs, const LinkTimingValues& timing,
                                xthread::RtStats* stats) noexcept {
    if (gpo == nullptr || gpi == nullptr) {
        fatal(Error{Errc::io, ERR_SITE(), 0}, "SpiBus::attach_registers");
    }
    SpiBus bus;
    bus.gpo_reg_ = gpo;
    bus.gpi_reg_ = gpi;
    bus.obs_ = obs;
    bus.stats_ = stats;
    bus.ack_soft_spins_ = timing.ack_soft_spins;
    bus.ack_timeout_ns_ = timing.ack_timeout_ns;
    bus.commit_gpo(0);
    return bus;
}

bool SpiBus::ready() const {
    TASTY_SEAT_BODY(SpiBus);

    return (read_gpi() & kNotReady) == 0u;
}

SpiSample SpiBus::sample() const noexcept {
    const std::uint32_t gpi = read_gpi();
    return SpiSample{
        .ready = (gpi & kNotReady) == 0u,
        .buttons = static_cast<std::uint16_t>((gpi >> 29) & 3u),
        .hdmi = ((gpi >> 20) & 1u) != 0u,
    };
}

Ex<CoreIdentity> SpiBus::identify() {

    const std::uint32_t pre = read_gpi();
    if ((pre & kNotReady) != 0u) {
        return std::unexpected(Error{Errc::fpga_not_ready, ERR_SITE(), pre});
    }

    const std::uint32_t id_view = gpo_.v & ~kCoreEnable;
    commit_gpo(id_view);
    const std::uint32_t sample = read_gpi();
    commit_gpo(id_view | kCoreEnable);

    if ((sample >> 8) != kCoreIdMagic) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), sample});
    }
    const auto type_byte = static_cast<std::uint8_t>(sample & 0xFFu);
    return CoreIdentity{type_byte, type_byte == kCoreTypeDualSdram};
}

CoreCapabilities SpiBus::capabilities() const {

    if ((gpo_.v & kCoreEnable) == 0u) {
        fatal(Error{Errc::negotiation, ERR_SITE(), gpo_.v},
              "SpiBus::capabilities() sampled with core-enable low");
    }

    const std::uint32_t gpi = read_gpi();
    CoreCapabilities caps{};
    caps.width = ((gpi >> 16) & 1u) != 0u ? Width::Word : Width::Byte;
    caps.io_version = static_cast<std::uint8_t>((gpi >> 18) & 3u);
    caps.board_digital = ((gpi >> 28) & 1u) != 0u;
    caps.button_osd = ((gpi >> 29) & 1u) != 0u;
    caps.button_user = ((gpi >> 30) & 1u) != 0u;
    caps.free_caps = static_cast<std::uint8_t>((gpi >> 21) & 0x7Fu);

    width_ = caps.width;
    io_version_ = caps.io_version;
    return caps;
}

void SpiBus::set_core_reset(bool asserted) {

    const std::uint32_t base = gpo_.v & ~(kCoreReset | kCoreEnable);
    commit_gpo(base | (asserted ? kCoreReset : kCoreEnable));
}

void SpiBus::clear_gpo() { commit_gpo(0); }

void SpiBus::select(ChipSelect cs) {

    std::uint32_t v = (gpo_.v | kCoreEnable) & ~kCsMask;
    switch (cs) {
        case ChipSelect::Fpga:
            v |= kFpgaEnable;
            break;
        case ChipSelect::Osd:
            v |= kOsdEnable;
            break;
        case ChipSelect::Io:
            v |= kIoEnable;
            break;
        case ChipSelect::None:
            break;

        case ChipSelect::OsdVga:
            v |= kOsdEnable | kFpgaEnable;
            break;
        case ChipSelect::OsdHdmi:
            v |= kOsdEnable | kIoEnable;
            break;
    }
    commit_gpo(v);
}

void SpiBus::deselect() { commit_gpo((gpo_.v | kCoreEnable) & ~kCsMask); }

#ifndef NDEBUG
void SpiBus::txn_enter() noexcept {
    if (txn_depth_ != 0) {
        fatal(Error{Errc::negotiation, ERR_SITE(), txn_depth_}, "SpiBus: nested Transaction");
    }
    ++txn_depth_;
    txn_cs_entry_ = gpo_.v & kCsMask;
}

void SpiBus::txn_exit() noexcept {
    --txn_depth_;
    const std::uint32_t cs_now = gpo_.v & kCsMask;
    if (cs_now != txn_cs_entry_) {
        fatal(Error{Errc::negotiation, ERR_SITE(), cs_now},
              "SpiBus: Transaction left the chip-select unbalanced");
    }
}
#endif

Ex<void> SpiBus::refuse_if_posted_() const noexcept {
    if (post_ == Post::None) return {};
    return std::unexpected(Error{Errc::would_block, ERR_SITE(), static_cast<std::uint32_t>(post_)});
}

Ex<SpiWord> SpiBus::transfer(SpiWord out) {
    if (auto held = refuse_if_posted_(); !held) return std::unexpected(held.error());

    const std::uint32_t gpo = (gpo_.v & ~(kData | kStrobe)) | out.v;
    commit_gpo(gpo);
    commit_gpo(gpo | kStrobe);
    if (auto ack = spin_ack(true); !ack) {

        commit_gpo(gpo);
        return std::unexpected(ack.error());
    }
    commit_gpo(gpo);
    const auto sample = spin_ack(false);
    if (!sample) return std::unexpected(sample.error());

    return SpiWord{static_cast<std::uint16_t>(*sample & kData)};
}

Ex<void> SpiBus::post(SpiWord out) {
    TASTY_SEAT_BODY(SpiBus);
    if (auto held = refuse_if_posted_(); !held) return held;
    const std::uint32_t gpo = (gpo_.v & ~(kData | kStrobe)) | out.v;
    commit_gpo(gpo);
    commit_gpo(gpo | kStrobe);
    post_ = Post::Rising;
    return {};
}

Ex<bool> SpiBus::posted_done() {
    TASTY_SEAT_BODY(SpiBus);
    if (post_ == Post::None) return true;
    std::uint32_t gpi = read_gpi();
    if (post_ == Post::Rising && (gpi & kNotReady) == 0u) {
        if ((gpi & kAck) == 0u) return false;
        commit_gpo(gpo_.v & ~kStrobe);
        post_ = Post::Falling;
        gpi = read_gpi();
    }
    if ((gpi & kNotReady) != 0u) {
        abandon_post();
        return std::unexpected(Error{Errc::fpga_not_ready, ERR_SITE(), gpi});
    }
    if ((gpi & kAck) != 0u) return false;
    post_ = Post::None;
    return true;
}

void SpiBus::abandon_post() noexcept {
    TASTY_SEAT_BODY(SpiBus);
    if (post_ == Post::None) return;
    commit_gpo(gpo_.v & ~kStrobe);
    post_ = Post::None;
}

Ex<void> SpiBus::block_write(std::span<const std::uint8_t> data) {
    if (auto held = refuse_if_posted_(); !held) return held;
    const std::uint32_t base = gpo_.v & ~(kData | kStrobe);
    std::uint32_t gpo = base;
    if (width_ == Width::Word) {
        const std::size_t n = data.size() / 2u;
        for (std::size_t i = 0; i < n; ++i) {
            gpo = base | static_cast<std::uint32_t>(data[2u * i]) |
                  (static_cast<std::uint32_t>(data[2u * i + 1u]) << 8);
            store_gpo(gpo);
            store_gpo(gpo | kStrobe);
        }
    } else {
        for (const std::uint8_t byte : data) {
            gpo = base | static_cast<std::uint32_t>(byte);
            store_gpo(gpo);
            store_gpo(gpo | kStrobe);
        }
    }

    commit_gpo(gpo);
    return {};
}

Ex<void> SpiBus::block_read(std::span<std::uint8_t> data) {
    if (auto held = refuse_if_posted_(); !held) return held;
    const std::uint32_t gpo = gpo_.v & ~(kData | kStrobe);
    const std::size_t n = (width_ == Width::Word) ? data.size() / 2u : data.size();
    if (n == 0) return {};

    if (width_ == Width::Word) {
        for (std::size_t i = 0; i < n; ++i) {
            store_gpo(gpo | kStrobe);
            store_gpo(gpo);
            const std::uint32_t gpi = read_gpi();
            data[2u * i] = static_cast<std::uint8_t>(gpi & 0xFFu);
            data[2u * i + 1u] = static_cast<std::uint8_t>((gpi >> 8) & 0xFFu);
        }
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            store_gpo(gpo | kStrobe);
            store_gpo(gpo);
            data[i] = static_cast<std::uint8_t>(read_gpi() & 0xFFu);
        }
    }

    gpo_.v = gpo;
    return {};
}

Ex<ButtonMask> SpiBus::buttons() {
    commit_gpo(gpo_.v | kCoreEnable);
    const std::uint32_t gpi = read_gpi();
    if ((gpi & kNotReady) != 0u) {

        return std::unexpected(Error{Errc::fpga_not_ready, ERR_SITE(), gpi});
    }
    return ButtonMask{static_cast<std::uint16_t>((gpi >> 29) & 3u)};
}

void SpiBus::set_disk_led(bool on) { commit_gpo(on ? (gpo_.v | kDiskLed) : (gpo_.v & ~kDiskLed)); }

}  // namespace mister::hal
