// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "infra/error.h"
#include "hal/link_port.h"
#include "hal/link_timing.h"
#include "hal/mailbox.h"
#include "hal/spi_output_observer.h"
#include "infra/seat.h"

namespace mister::xthread {
struct RtStats;
}

namespace mister::hal {

inline constexpr std::uint32_t kCoreIdMagic = 0x5CA623u;

class SpiBus final : public ILinkPort {
    TASTY_SEAT_RESIDENT(RT);

public:
    [[nodiscard]] static Ex<SpiBus> open(Mailbox mailbox, xthread::RtStats* stats = nullptr);

    static SpiBus attach_registers(volatile std::uint32_t* gpo, const volatile std::uint32_t* gpi,
                                   ISpiOutputObserver* obs, const LinkTimingValues& timing,
                                   xthread::RtStats* stats = nullptr) noexcept;

    ~SpiBus() override = default;
    SpiBus(SpiBus&&) noexcept = default;
    SpiBus& operator=(SpiBus&&) noexcept = default;

    Ex<CoreIdentity> identify() override;
    CoreCapabilities latch_capabilities() override;
    void set_core_reset(bool asserted) override;
    void clear_gpo() override;

    void select(ChipSelect cs) override;
    void deselect() override;
    Ex<SpiWord> transfer(SpiWord out) override;
    Width width() const noexcept override {
        TASTY_SEAT_BODY(SpiBus);
        return width_;
    }
    std::uint8_t io_version() const noexcept override {
        TASTY_SEAT_BODY(SpiBus);
        return io_version_;
    }
    Ex<void> block_write(std::span<const std::uint8_t> data) override;
    Ex<void> block_read(std::span<std::uint8_t> data) override;

    [[nodiscard]] Ex<void> post(SpiWord out) override;
    [[nodiscard]] Ex<bool> posted_done() override;
    void abandon_post() noexcept override;

    bool ready() const override;
    Ex<ButtonMask> buttons();
    void set_disk_led(bool on);

    [[nodiscard]] SpiSample sample() const noexcept override;

    class Transaction {
    public:
        explicit Transaction(SpiBus& bus) {
#ifndef NDEBUG
            owner_ = &bus;
            bus.txn_enter();
#else
            (void)bus;
#endif
        }
        ~Transaction() {
#ifndef NDEBUG
            owner_->txn_exit();
#endif
        }
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;

#ifndef NDEBUG
    private:
        SpiBus* owner_ = nullptr;
#endif
    };

    SpiOutput gpo_shadow() const noexcept { return gpo_; }

private:
    SpiBus() = default;

    void store_gpo(std::uint32_t v) noexcept;
    void commit_gpo(std::uint32_t v) noexcept;
    std::uint32_t read_gpi() const noexcept;

    Ex<std::uint32_t> spin_ack(bool want) noexcept;

#ifndef NDEBUG
    void txn_enter() noexcept;
    void txn_exit() noexcept;
#endif

    enum class Post : std::uint8_t { None, Rising, Falling };
    [[nodiscard]] Ex<void> refuse_if_posted_() const noexcept;

    SpiOutput gpo_{};
    volatile std::uint32_t* gpo_reg_ = nullptr;
    const volatile std::uint32_t* gpi_reg_ = nullptr;

    Width width_ = Width::Byte;
    std::uint8_t io_version_ = 0;
    ISpiOutputObserver* obs_ = nullptr;
    xthread::RtStats* stats_ = nullptr;
    Post post_ = Post::None;
    MailboxClaims::Claim mailbox_;
#ifndef NDEBUG
    std::uint32_t txn_depth_ = 0;
    std::uint32_t txn_cs_entry_ = 0;
#endif

    unsigned ack_soft_spins_ = 0;
    std::uint64_t ack_timeout_ns_ = 0;
};

}  // namespace mister::hal
