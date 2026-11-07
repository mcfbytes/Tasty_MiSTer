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

[[nodiscard]] constexpr SpiOutput chip_selects(SpiOutput w) noexcept {
    return SpiOutput{w.v & gpo::kCsMask.v};
}

std::uint64_t now_ns() noexcept {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull
        + static_cast<std::uint64_t>(ts.tv_nsec);
}

}

void SpiBus::store_gpo(SpiOutput v) noexcept {
    gpo_reg_.write(v.v);
    if (obs_)
        obs_->on_store(v);
}

void SpiBus::commit_gpo(SpiOutput v) noexcept {
    gpo_ = v;
    store_gpo(v);
}

GpiWord SpiBus::read_gpi() const noexcept {
    return GpiWord{gpi_reg_.read()};
}

Ex<GpiWord> SpiBus::spin_ack(bool want, bool counts) noexcept {
    unsigned budget = ack_soft_spins_;
    std::uint64_t limit = 0;
    bool armed = false;
    for (;;) {
        GpiWord gpi = read_gpi();
        if (gpi.test(kGpiNotReadyField)) {

            return std::unexpected(Error{Errc::fpga_not_ready, ERR_SITE(), detail_of(gpi)});
        }
        if (gpi.test(kGpiAckField) == want)
            return gpi;
        if (--budget != 0u)
            continue;
        budget = ack_soft_spins_;
        const std::uint64_t now = now_ns();
        if (!armed) {
            limit = now + (start_bound_ ? start_ack_timeout_ns_ : ack_timeout_ns_);
            armed = true;
            continue;
        }
        if (now >= limit) {
            if (counts && stats_)
                stats_->spin_timeouts.add();
            return std::unexpected(Error{Errc::spi_timeout, ERR_SITE(), detail_of(gpi)});
        }
    }
}

Ex<SpiBus> SpiBus::open(Mailbox mailbox, xthread::RtStats* stats) {

    if (!mailbox.held())
        return std::unexpected(Error{Errc::busy, ERR_SITE(), 0});
    const LinkTimingValues& timing = mailbox.timing();

    SpiBus bus;
    bus.gpo_reg_ = mailbox.gpo();
    bus.gpi_reg_ = mailbox.gpi();
    bus.stats_ = infra::opt_ref_of(stats);
    bus.ack_soft_spins_ = timing.ack_soft_spins;
    bus.ack_timeout_ns_ = timing.ack_timeout_ns;
    bus.start_ack_timeout_ns_ = timing.start_ack_timeout_ns;

    bus.mailbox_ = std::move(mailbox).consume();

    bus.commit_gpo(SpiOutput{});
    return bus;
}

SpiBus SpiBus::attach_registers(
    reg::Reg<reg::WO> gpo,
    reg::Reg<reg::RO> gpi,
    ISpiOutputObserver* obs,
    const LinkTimingValues& timing,
    xthread::RtStats* stats
) noexcept {
    if (!gpo.bound() || !gpi.bound()) {
        fatal(Error{Errc::io, ERR_SITE(), 0}, "SpiBus::attach_registers");
    }
    SpiBus bus;
    bus.gpo_reg_ = gpo;
    bus.gpi_reg_ = gpi;
    bus.obs_ = infra::opt_ref_of(obs);
    bus.stats_ = infra::opt_ref_of(stats);
    bus.ack_soft_spins_ = timing.ack_soft_spins;
    bus.ack_timeout_ns_ = timing.ack_timeout_ns;
    bus.start_ack_timeout_ns_ = timing.start_ack_timeout_ns;
    bus.commit_gpo(SpiOutput{});
    return bus;
}

bool SpiBus::ready() const {
    TASTY_SEAT_BODY(SpiBus);

    return !read_gpi().test(kGpiNotReadyField);
}

SpiSample SpiBus::sample() const noexcept {
    return decode_sample(read_gpi());
}

Ex<CoreIdentity> SpiBus::identify() {

    const GpiWord pre = read_gpi();
    if (pre.test(kGpiNotReadyField)) {
        return std::unexpected(Error{Errc::fpga_not_ready, ERR_SITE(), detail_of(pre)});
    }

    const SpiOutput id_view = gpo_.without(gpo::kCoreEnable);
    commit_gpo(id_view);
    const GpiWord sample = read_gpi();
    commit_gpo(id_view.with(gpo::kCoreEnable));

    if (sample.field(kGpiCoreIdField) != kCoreIdMagic) {
        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), detail_of(sample)});
    }
    const CoreTypeByte type{static_cast<std::uint8_t>(sample.field(kGpiTypeByteField))};
    return CoreIdentity{type};
}

CoreCapabilities SpiBus::latch_capabilities() {

    if (!gpo_.has(gpo::kCoreEnable)) {
        fatal(
            Error{Errc::negotiation, ERR_SITE(), gpo_.v},
            "SpiBus::latch_capabilities() sampled with core-enable low"
        );
    }

    const GpiWord gpi = read_gpi();
    CoreCapabilities caps{};
    caps.width = gpi.test(kGpiWidthField) ? Width::Word : Width::Byte;
    caps.io_version = IoVersion{static_cast<std::uint8_t>(gpi.field(kGpiIoVersionField))};
    caps.board_digital = gpi.test(kGpiBoardDigitalField);
    caps.button_osd = gpi.test(kGpiButtonOsdField);
    caps.button_user = gpi.test(kGpiButtonUserField);
    caps.free_caps = static_cast<std::uint8_t>(gpi.field(kGpiFreeCapsField));
    caps.lw_directory = gpi.test(kGpiDirectoryField);

    width_ = caps.width;
    io_version_ = caps.io_version;
    return caps;
}

void SpiBus::set_core_reset(bool asserted) {

    const SpiOutput base = gpo_.without(gpo::kCoreReset).without(gpo::kCoreEnable);
    commit_gpo(base.with(asserted ? gpo::kCoreReset : gpo::kCoreEnable));
}

void SpiBus::clear_gpo() {
    commit_gpo(SpiOutput{});
}

void SpiBus::select(ChipSelect cs) {

    SpiOutput v = gpo_.with(gpo::kCoreEnable).without(gpo::kCsMask);
    switch (cs) {
        case ChipSelect::Fpga:
            v = v.with(gpo::kFpgaEnable);
            break;
        case ChipSelect::Osd:
            v = v.with(gpo::kOsdEnable);
            break;
        case ChipSelect::Io:
            v = v.with(gpo::kIoEnable);
            break;
        case ChipSelect::None:
            break;

        case ChipSelect::OsdVga:
            v = v.with(gpo::kOsdEnable).with(gpo::kFpgaEnable);
            break;
        case ChipSelect::OsdHdmi:
            v = v.with(gpo::kOsdEnable).with(gpo::kIoEnable);
            break;
    }
    commit_gpo(v);
}

void SpiBus::deselect() {
    commit_gpo(gpo_.with(gpo::kCoreEnable).without(gpo::kCsMask));
}

Ex<void> SpiBus::refuse_if_posted_() const noexcept {
    if (post_ == Post::None)
        return {};
    return std::unexpected(Error{Errc::would_block, ERR_SITE(), static_cast<std::uint32_t>(post_)});
}

Ex<SpiWord> SpiBus::transfer(SpiWord out) {
    if (auto held = refuse_if_posted_(); !held)
        return std::unexpected(held.error());
    return handshake_(out, false);
}

Ex<bool> SpiBus::transfer_or_post(SpiWord out) {
    TASTY_SEAT_BODY(SpiBus);
    if (auto held = refuse_if_posted_(); !held)
        return std::unexpected(held.error());
    auto r = handshake_(out, true);
    if (r)
        return true;
    if (post_ != Post::None)
        return false;
    return std::unexpected(r.error());
}

Ex<SpiWord> SpiBus::handshake_(SpiWord out, bool guarded) noexcept {

    const SpiOutput gpo = gpo_.without(gpo::kData).without(gpo::kStrobe).with_data(out);
    commit_gpo(gpo);
    commit_gpo(gpo.with(gpo::kStrobe));
    if (auto ack = spin_ack(true, !guarded); !ack) {
        if (guarded && ack.error().code == Errc::spi_timeout) {
            post_ = Post::Rising;
            return std::unexpected(ack.error());
        }

        commit_gpo(gpo);
        return std::unexpected(ack.error());
    }
    commit_gpo(gpo);
    const auto sample = spin_ack(false, !guarded);
    if (!sample) {
        if (guarded && sample.error().code == Errc::spi_timeout)
            post_ = Post::Falling;
        return std::unexpected(sample.error());
    }

    return gpi_data(*sample);
}

Ex<void> SpiBus::post(SpiWord out) {
    TASTY_SEAT_BODY(SpiBus);
    if (auto held = refuse_if_posted_(); !held)
        return held;
    const SpiOutput gpo = gpo_.without(gpo::kData).without(gpo::kStrobe).with_data(out);
    commit_gpo(gpo);
    commit_gpo(gpo.with(gpo::kStrobe));
    post_ = Post::Rising;
    return {};
}

Ex<bool> SpiBus::posted_done() {
    TASTY_SEAT_BODY(SpiBus);
    if (post_ == Post::None)
        return true;
    GpiWord gpi = read_gpi();
    if (post_ == Post::Rising && !gpi.test(kGpiNotReadyField)) {
        if (!gpi.test(kGpiAckField))
            return false;
        commit_gpo(gpo_.without(gpo::kStrobe));
        post_ = Post::Falling;
        gpi = read_gpi();
    }
    if (gpi.test(kGpiNotReadyField)) {
        abandon_post();
        return std::unexpected(Error{Errc::fpga_not_ready, ERR_SITE(), detail_of(gpi)});
    }
    if (gpi.test(kGpiAckField))
        return false;
    post_ = Post::None;
    return true;
}

void SpiBus::abandon_post() noexcept {
    TASTY_SEAT_BODY(SpiBus);
    if (post_ == Post::None)
        return;
    commit_gpo(gpo_.without(gpo::kStrobe));
    post_ = Post::None;
}

Ex<void> SpiBus::block_write(std::span<const std::uint8_t> data) {
    if (auto held = refuse_if_posted_(); !held)
        return held;
    const SpiOutput base = gpo_.without(gpo::kData).without(gpo::kStrobe);
    SpiOutput gpo = base;
    if (width_ == Width::Word) {
        const std::size_t n = data.size() / 2u;
        for (std::size_t i = 0; i < n; ++i) {
            const auto word = static_cast<std::uint16_t>(
                static_cast<std::uint32_t>(data[2u * i])
                | (static_cast<std::uint32_t>(data[2u * i + 1u]) << 8)
            );
            gpo = base.with_data(SpiWord{word});
            store_gpo(gpo);
            store_gpo(gpo.with(gpo::kStrobe));
        }
    } else {
        for (const std::uint8_t byte : data) {
            gpo = base.with_data(SpiWord{byte});
            store_gpo(gpo);
            store_gpo(gpo.with(gpo::kStrobe));
        }
    }

    commit_gpo(gpo);
    return {};
}

Ex<void> SpiBus::block_read(std::span<std::uint8_t> data) {
    if (auto held = refuse_if_posted_(); !held)
        return held;
    const SpiOutput gpo = gpo_.without(gpo::kData).without(gpo::kStrobe);
    const std::size_t n = (width_ == Width::Word) ? data.size() / 2u : data.size();
    if (n == 0)
        return {};

    if (width_ == Width::Word) {
        for (std::size_t i = 0; i < n; ++i) {
            store_gpo(gpo.with(gpo::kStrobe));
            store_gpo(gpo);
            const SpiWord w = gpi_data(read_gpi());
            data[2u * i] = static_cast<std::uint8_t>(w.v);
            data[2u * i + 1u] = static_cast<std::uint8_t>(w.v >> 8);
        }
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            store_gpo(gpo.with(gpo::kStrobe));
            store_gpo(gpo);
            data[i] = static_cast<std::uint8_t>(gpi_data(read_gpi()).v);
        }
    }

    gpo_ = gpo;
    return {};
}

Ex<ButtonMask> SpiBus::buttons() {
    commit_gpo(gpo_.with(gpo::kCoreEnable));
    const GpiWord gpi = read_gpi();
    if (gpi.test(kGpiNotReadyField)) {

        return std::unexpected(Error{Errc::fpga_not_ready, ERR_SITE(), detail_of(gpi)});
    }
    return gpi.buttons();
}

void SpiBus::set_disk_led(bool on) {
    commit_gpo(on ? gpo_.with(gpo::kDiskLed) : gpo_.without(gpo::kDiskLed));
}

}
