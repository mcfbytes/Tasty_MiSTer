// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <cstdint>

#include "infra/error.h"
#include "infra/opt_ref.h"
#include "hal/block_view.h"
#include "hal/gpi_block.h"
#include "hal/gpo_block.h"
#include "hal/board_link.h"
#include "hal/link_timing.h"
#include "hal/lw_presence.h"
#include "hal/gpo_gpi_pair.h"
#include "hal/ocram_run.h"
#include "hal/gpo_word.h"
#include "hal/gpo_observer.h"
#include "hal/pin_sample.h"
#include "infra/prelude.h"
#include "infra/seat.h"

namespace mister::infra::xthread {
struct RtStats;
}

namespace mister::hal {

inline constexpr std::uint32_t kCoreIdMagic = 0x5CA623u;

namespace gpo {
inline constexpr GpoWord kData{GpoWord::kData};
inline constexpr GpoWord kStrobe{1u << 17};
inline constexpr GpoWord kFpgaEnable{1u << 18};
inline constexpr GpoWord kOsdEnable{1u << 19};
inline constexpr GpoWord kIoEnable{1u << 20};
inline constexpr GpoWord kOcramRun{1u << 28};
inline constexpr GpoWord kDiskLed{1u << 29};
inline constexpr GpoWord kCoreReset{1u << 30};
inline constexpr GpoWord kCoreEnable{1u << 31};
inline constexpr GpoWord kCsMask{kFpgaEnable.with(kOsdEnable).with(kIoEnable)};
}

enum class LinkLook : std::uint8_t { IdView, NoLwHost, LwHostEnabled };

[[nodiscard]] LinkLook look(GpoWord gpo_register, GpiWord gpi) noexcept;

enum class LookPolicy : std::uint8_t { TakeOver, StopOnHost };

struct GpoGpiPortOpen;

class GpoGpiPort final : public IBoardLink, public IOcramRun {
    TASTY_SEAT_RESIDENT(RT);

public:
    [[nodiscard]] static Ex<GpoGpiPortOpen> open(
        GpoGpiPair mailbox,
        LookPolicy policy,
        xthread::RtStats* stats = nullptr
    );

    ~GpoGpiPort() override = default;
    GpoGpiPort(GpoGpiPort&&) noexcept = default;
    GpoGpiPort& operator=(GpoGpiPort&&) noexcept = default;

    [[nodiscard]] Ex<CoreTypeByte> identify() override;
    CoreCapabilities latch_capabilities() override;
    void set_core_reset(bool asserted) override;
    void clear_signals() override;

    void select(ChipSelect cs) override;
    void deselect() override;
    [[nodiscard]] Ex<LinkWord> transfer(LinkWord out) override;

    Width width() const noexcept override {
        TASTY_SEAT_BODY(GpoGpiPort);
        return width_;
    }

    IoVersion io_version() const noexcept override {
        TASTY_SEAT_BODY(GpoGpiPort);
        return io_version_;
    }

    [[nodiscard]] Ex<void> block_write(std::span<const std::uint8_t> data) override;
    [[nodiscard]] Ex<void> block_read(std::span<std::uint8_t> data) override;

    [[nodiscard]] Ex<void> post(LinkWord out) override;
    [[nodiscard]] Ex<bool> posted_done() override;
    void abandon_post() noexcept override;

    [[nodiscard]] Ex<bool> transfer_or_post(LinkWord out) override;

    [[nodiscard]] bool posted() const noexcept override {
        TASTY_SEAT_BODY(GpoGpiPort);
        return post_ != Post::None;
    }

    bool ready() const override;
    [[nodiscard]] Ex<ButtonMask> buttons();
    void set_disk_led(bool on);

    void ocram_run(OcramRunEdge edge) override;

    [[nodiscard]] PinSample sample() const noexcept override;

    class Transaction {
        TASTY_SEAT_RESIDENT(RT);

    public:
        explicit Transaction(GpoGpiPort& bus) { (void)bus; }

        ~Transaction() = default;
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;
    };

    GpoWord gpo_shadow() const noexcept { return gpo_; }

private:
    friend struct GpoGpiPortTestAccess;

    GpoGpiPort() = default;

    [[nodiscard]] static Ex<GpoGpiPortOpen> attach_registers(
        BlockView<GpoBlock> gpo,
        BlockView<GpiBlock> gpi,
        IGpoObserver* obs,
        const LinkTimingValues& timing,
        LookPolicy policy,
        xthread::RtStats* stats = nullptr
    ) noexcept;

    [[nodiscard]] LinkLook look_at_open_() noexcept;

    void store_gpo(GpoWord v) noexcept;
    void commit_gpo(GpoWord v) noexcept;
    [[nodiscard]] GpiWord read_gpi() const noexcept;

    enum class AckLevel : std::uint8_t { Raised, Dropped };

    enum class Timeout : std::uint8_t { Counted, Guarded };

    [[nodiscard]] Ex<GpiWord> spin_ack(
        AckLevel want,
        Timeout on_timeout = Timeout::Counted
    ) noexcept;

    enum class Post : std::uint8_t { None, Rising, Falling };
    [[nodiscard]] Ex<void> refuse_if_posted_() const noexcept;

    [[nodiscard]] Ex<LinkWord> handshake_(LinkWord out, bool guarded) noexcept;

    GpoWord gpo_{};
    BlockView<GpoBlock> gpo_reg_;
    BlockView<GpiBlock> gpi_reg_;

    Width width_ = Width::Byte;
    IoVersion io_version_{};
    infra::OptRef<IGpoObserver> obs_{};
    infra::OptRef<xthread::RtStats> stats_{};
    Post post_ = Post::None;
    GpoGpiClaims::Claim mailbox_;

    unsigned ack_soft_spins_ = 0;
    std::chrono::nanoseconds ack_timeout_ns_{};
};

struct GpoGpiPortOpen {
    GpoGpiPort bus;
    LinkLook look = LinkLook::IdView;
};

}
