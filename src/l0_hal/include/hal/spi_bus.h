// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "hal/link_port.h"
#include "hal/link_timing.h"
#include "hal/mailbox.h"
#include "hal/reg.h"
#include "hal/spi_output_observer.h"
#include "hal/spi_sample.h"
#include "infra/seat.h"

namespace mister::xthread {
struct RtStats;
}

namespace mister::hal {

inline constexpr std::uint32_t kCoreIdMagic = 0x5CA623u;

namespace gpo {
inline constexpr SpiOutput kData{SpiOutput::kData};
inline constexpr SpiOutput kStrobe{1u << 17};
inline constexpr SpiOutput kFpgaEnable{1u << 18};
inline constexpr SpiOutput kOsdEnable{1u << 19};
inline constexpr SpiOutput kIoEnable{1u << 20};
inline constexpr SpiOutput kDiskLed{1u << 29};
inline constexpr SpiOutput kCoreReset{1u << 30};
inline constexpr SpiOutput kCoreEnable{1u << 31};
inline constexpr SpiOutput kCsMask{kFpgaEnable.with(kOsdEnable).with(kIoEnable)};
}

class SpiBus final : public ILinkPort {
    TASTY_SEAT_RESIDENT(RT);

public:
    [[nodiscard]] static Ex<SpiBus> open(Mailbox mailbox, xthread::RtStats* stats = nullptr);

    ~SpiBus() override = default;
    SpiBus(SpiBus&&) noexcept = default;
    SpiBus& operator=(SpiBus&&) noexcept = default;

    [[nodiscard]] Ex<CoreIdentity> identify() override;
    CoreCapabilities latch_capabilities() override;
    void set_core_reset(bool asserted) override;
    void clear_gpo() override;

    void select(ChipSelect cs) override;
    void deselect() override;
    [[nodiscard]] Ex<SpiWord> transfer(SpiWord out) override;

    Width width() const noexcept override {
        TASTY_SEAT_BODY(SpiBus);
        return width_;
    }

    IoVersion io_version() const noexcept override {
        TASTY_SEAT_BODY(SpiBus);
        return io_version_;
    }

    [[nodiscard]] Ex<void> block_write(std::span<const std::uint8_t> data) override;
    [[nodiscard]] Ex<void> block_read(std::span<std::uint8_t> data) override;

    [[nodiscard]] Ex<void> post(SpiWord out) override;
    [[nodiscard]] Ex<bool> posted_done() override;
    void abandon_post() noexcept override;

    [[nodiscard]] Ex<bool> transfer_or_post(SpiWord out) override;

    [[nodiscard]] bool posted() const noexcept override {
        TASTY_SEAT_BODY(SpiBus);
        return post_ != Post::None;
    }

    bool ready() const override;
    [[nodiscard]] Ex<ButtonMask> buttons();
    void set_disk_led(bool on);

    [[nodiscard]] SpiSample sample() const noexcept override;

    class Transaction {
        TASTY_SEAT_RESIDENT(RT);

    public:
        explicit Transaction(SpiBus& bus) { (void)bus; }

        ~Transaction() = default;
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;
    };

    SpiOutput gpo_shadow() const noexcept { return gpo_; }

private:
    friend struct SpiBusTestAccess;

    SpiBus() = default;

    static SpiBus attach_registers(
        reg::Reg<reg::WO> gpo,
        reg::Reg<reg::RO> gpi,
        ISpiOutputObserver* obs,
        const LinkTimingValues& timing,
        xthread::RtStats* stats = nullptr
    ) noexcept;

    void store_gpo(SpiOutput v) noexcept;
    void commit_gpo(SpiOutput v) noexcept;
    [[nodiscard]] GpiWord read_gpi() const noexcept;

    enum class AckLevel : std::uint8_t { Raised, Dropped };

    enum class Timeout : std::uint8_t { Counted, Guarded };

    [[nodiscard]] Ex<GpiWord> spin_ack(
        AckLevel want,
        Timeout on_timeout = Timeout::Counted
    ) noexcept;

    enum class Post : std::uint8_t { None, Rising, Falling };
    [[nodiscard]] Ex<void> refuse_if_posted_() const noexcept;

    [[nodiscard]] Ex<SpiWord> handshake_(SpiWord out, bool guarded) noexcept;

    SpiOutput gpo_{};
    reg::Reg<reg::WO> gpo_reg_;
    reg::Reg<reg::RO> gpi_reg_;

    Width width_ = Width::Byte;
    IoVersion io_version_{};
    infra::OptRef<ISpiOutputObserver> obs_{};
    infra::OptRef<xthread::RtStats> stats_{};
    Post post_ = Post::None;
    MailboxClaims::Claim mailbox_;

    unsigned ack_soft_spins_ = 0;
    std::chrono::nanoseconds ack_timeout_ns_{};
};

}
