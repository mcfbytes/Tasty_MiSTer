// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "hal/video_out_decl.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "os/types.h"
#include "svc/adv7513_probe_report.h"
#include "svc/adv7513_reg_write.h"
#include "svc/adv7513_spd_packet.h"

namespace mister::svc {
struct Modeline;
}

namespace mister::svc::adv7513 {

class II2cAdapter;
struct AttachOutcome;
struct InitOptions;

inline constexpr bool kAdvModeWrite = true;

inline constexpr os::I2cReg kRegPacketEnable{0x40};
inline constexpr std::uint8_t kPacketMaskSpd = 0x40;
inline constexpr os::I2cReg kPacketOffsetSpd{0x00};

SpdPacket build_standard_spd(std::string_view core_name);

inline constexpr std::uint8_t kHpdBit = 0x40;
inline constexpr std::uint8_t kMonitorSenseBit = 0x20;

struct LinkStatus {
    std::uint8_t v = 0;

    [[nodiscard]] constexpr bool hpd() const noexcept { return (v & kHpdBit) != 0; }

    [[nodiscard]] constexpr bool monitor_sense() const noexcept {
        return (v & kMonitorSenseBit) != 0;
    }

    [[nodiscard]] constexpr bool sink_present() const noexcept { return hpd() && monitor_sense(); }

    friend constexpr bool operator==(LinkStatus, LinkStatus) = default;
};

inline constexpr std::uint8_t kHotplugBits = 0xC0;
inline constexpr std::uint8_t kEdidReadyBit = 0x04;

struct IntBits {
    std::uint8_t v = 0;

    [[nodiscard]] static constexpr IntBits hotplug() noexcept { return IntBits{kHotplugBits}; }

    [[nodiscard]] constexpr IntBits pending_hotplug() const noexcept {
        return IntBits{static_cast<std::uint8_t>(v & kHotplugBits)};
    }

    [[nodiscard]] constexpr bool edid_ready() const noexcept { return (v & kEdidReadyBit) != 0; }

    [[nodiscard]] constexpr bool none() const noexcept { return v == 0; }

    [[nodiscard]] constexpr RegWrite write_row(os::I2cReg reg) const noexcept {
        return RegWrite{reg, v};
    }

    friend constexpr bool operator==(IntBits, IntBits) = default;
};

class Adv7513Bus {
    TASTY_SEAT_RESIDENT(Ui);

public:
    Adv7513Bus(const Adv7513Bus&) = delete;
    Adv7513Bus& operator=(const Adv7513Bus&) = delete;
    Adv7513Bus(Adv7513Bus&&) noexcept = default;

    Adv7513Bus& operator=(Adv7513Bus&&) = delete;

    [[nodiscard]] static AttachOutcome attach(II2cAdapter& a, const hal::VideoOutDecl& board);

    [[nodiscard]] Ex<void> configure(const InitOptions& o);

    [[nodiscard]] Ex<void> configure_audio(const InitOptions& o);

    [[nodiscard]] Ex<void> tmds_power(bool on);

    [[nodiscard]] Ex<LinkStatus> read_status();

    [[nodiscard]] Ex<void> arm_interrupts(bool on);

    [[nodiscard]] Ex<IntBits> read_interrupts();

    [[nodiscard]] Ex<void> clear_interrupts(IntBits pending);

    [[nodiscard]] Ex<void> set_mode(const Modeline& m, bool direct_video_menu);

    [[nodiscard]] Ex<void> set_packet(
        std::uint8_t mask,
        os::I2cReg first_reg,
        std::optional<std::span<const std::uint8_t>> payload
    );

    [[nodiscard]] Ex<void> spd_config(const SpdPacket& p);
    [[nodiscard]] Ex<void> spd_disable();

    struct Verify {
        std::uint8_t r41 = 0, r17 = 0, r3b = 0, r3c = 0;
        bool matched = false;
    };

    [[nodiscard]] Ex<Verify> verify(const Modeline& m, bool direct_video_menu);

    struct ModeBytes {
        std::uint8_t sync_invert = 0;
        std::uint8_t pr_flags = 0;
        std::uint8_t vic = 0;
        friend constexpr bool operator==(ModeBytes, ModeBytes) = default;
    };

    unsigned bus() const noexcept { return bus_; }

    std::uint32_t writes() const noexcept { return writes_; }

    std::uint32_t errors() const noexcept { return errors_; }

    [[nodiscard]] std::optional<os::I2cReg> fail_reg() const noexcept { return fail_reg_; }

    [[nodiscard]] std::optional<os::I2cReg> spd_fail_reg() const noexcept { return spd_fail_reg_; }

    bool mode_cached() const noexcept { return last_mode_.has_value(); }

private:
    Adv7513Bus(II2cAdapter& a, unsigned bus) noexcept : a_(a), bus_(bus) {}

    [[nodiscard]] Ex<void> write_rows(std::span<const RegWrite> rows);

    II2cAdapter& a_;
    unsigned bus_ = 0;

    std::optional<ModeBytes> last_mode_{};

    std::uint32_t writes_ = 0;
    std::uint32_t errors_ = 0;
    std::optional<os::I2cReg> fail_reg_{};
    std::optional<os::I2cReg> spd_fail_reg_{};
};

struct AttachOutcome {
    ProbeReport report;
    Ex<Adv7513Bus> bus;
};

}
