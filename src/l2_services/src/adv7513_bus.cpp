// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/adv7513_bus.h"

#include "hal/fact_field.h"

#include "svc/adv7513_io.h"
#include "svc/adv7513_linux_adapter.h"
#include "svc/adv7513_probe_report.h"
#include "svc/adv7513_reg_write.h"
#include "svc/i2c_adapter.h"
#include "svc/modeline.h"

#include <utility>

#include "video_service_detail.h"

namespace mister::svc::adv7513 {

namespace {

struct SubMapRow {
    SubMapRole role;
    os::I2cAddr addr;
};

constexpr std::array<SubMapRow, kSubMapCount> kSubMapRows{{
    {SubMapRole::Edid, kEdidAddr},
    {SubMapRole::Spd, kSpdAddr},
    {SubMapRole::Cec, kCecAddr},
}};

constexpr os::I2cReg reg_after(os::I2cReg base, std::size_t off) noexcept {
    return base.plus(off);
}

}

infra::OptRef<LinuxAdapter::Slot> LinuxAdapter::find(os::I2cAddr addr) noexcept {
    for (Slot& s : slots_) {
        if (s.dev && s.addr == addr)
            return infra::OptRef<Slot>{s};
    }
    return {};
}

Ex<std::uint8_t> LinuxAdapter::probe(unsigned bus, os::I2cAddr addr) {
    TASTY_SEAT_BODY(LinuxAdapter);

    auto dev = os::I2cDevice::open(bus, addr);
    if (!dev)
        return std::unexpected(dev.error());
    return dev->read_byte();
}

Ex<void> LinuxAdapter::open(unsigned bus, os::I2cAddr addr) {
    TASTY_SEAT_BODY(LinuxAdapter);
    if (find(addr))
        return {};
    for (Slot& s : slots_) {
        if (s.dev)
            continue;
        auto dev = os::I2cDevice::open(bus, addr);
        if (!dev)
            return std::unexpected(dev.error());
        s.addr = addr;
        s.dev.emplace(std::move(*dev));
        return {};
    }

    return err(Errc::bad_format, ERR_SITE(), addr_detail(addr));
}

Ex<std::uint8_t> LinuxAdapter::read(os::I2cAddr addr, os::I2cReg reg) {
    TASTY_SEAT_BODY(LinuxAdapter);
    const infra::OptRef<Slot> s = find(addr);
    if (!s)
        return err(Errc::not_found, ERR_SITE(), addr_detail(addr));
    return s->dev->read_reg(reg);
}

Ex<void> LinuxAdapter::write(os::I2cAddr addr, os::I2cReg reg, std::uint8_t val) {
    TASTY_SEAT_BODY(LinuxAdapter);
    const infra::OptRef<Slot> s = find(addr);
    if (!s)
        return err(Errc::not_found, ERR_SITE(), addr_detail(addr));
    return s->dev->write_reg(reg, val);
}

AttachOutcome Adv7513Bus::attach(II2cAdapter& a, const hal::VideoOutDecl& board) {
    ProbeReport out{};

    const auto range = board.i2c_buses.resolve(
        ERR_SITE(),
        static_cast<std::uint32_t>(hal::FactField::VideoI2cBuses)
    );
    if (!range) {
        out.presence = Presence::Absent;
        return AttachOutcome{std::move(out), std::unexpected(range.error())};
    }
    unsigned found = 0;
    for (unsigned bus = range->first; bus <= range->last; ++bus) {
        auto r = a.probe(bus, kMainAddr);
        if (r) {
            ++out.acks;
            if (out.acks == 1) {
                found = bus;
                out.bus = static_cast<std::uint8_t>(bus);
            }
        } else {
            out.last_errno = r.error().detail;
        }
    }
    if (out.acks == 0) {
        out.presence = Presence::Absent;
        return AttachOutcome{
            std::move(out),
            err(Errc::not_found, ERR_SITE(), addr_detail(kMainAddr))
        };
    }
    if (out.acks >= 2) {

        out.presence = Presence::Ambiguous;
        return AttachOutcome{std::move(out), err(Errc::bad_format, ERR_SITE(), out.acks)};
    }

    if (auto o = a.open(found, kMainAddr); !o) {
        out.presence = Presence::Rejected;
        return AttachOutcome{std::move(out), std::unexpected(o.error())};
    }

    auto r42 = a.read(kMainAddr, kRegStatus);
    if (!r42) {
        out.presence = Presence::Rejected;
        return AttachOutcome{std::move(out), std::unexpected(r42.error())};
    }
    auto r41 = a.read(kMainAddr, kRegPower);
    if (!r41) {
        out.presence = Presence::Rejected;
        return AttachOutcome{std::move(out), std::unexpected(r41.error())};
    }
    out.r42 = *r42;
    out.r41 = *r41;

    if ((out.r41 == 0xFF && out.r42 == 0xFF) || (out.r41 == 0x00 && out.r42 == 0x00)) {
        out.presence = Presence::Rejected;
        return AttachOutcome{
            std::move(out),
            err(Errc::bad_format,
                ERR_SITE(),
                static_cast<std::uint32_t>(out.r41) | (static_cast<std::uint32_t>(out.r42) << 8))
        };
    }

    for (const SubMapRow& s : kSubMapRows) {
        const bool acked = a.probe(found, s.addr).has_value();
        out.set_ack(s.role, acked && a.open(found, s.addr).has_value());
    }

    out.presence = Presence::Found;
    return AttachOutcome{std::move(out), Adv7513Bus{a, found}};
}

Ex<void> Adv7513Bus::write_rows(std::span<const RegWrite> rows) {
    for (const RegWrite& w : rows) {
        auto r = a_.write(kMainAddr, w.reg, w.val);
        if (!r) {

            ++errors_;
            if (!fail_reg_)
                fail_reg_ = w.reg;
            return err(r.error().code, ERR_SITE(), w.reg.errno_detail(r.error()));
        }
        ++writes_;
    }
    return {};
}

Ex<void> Adv7513Bus::configure(const InitOptions& o) {
    const auto table = init_table(o);
    if (auto r = write_rows(table); !r)
        return r;

    const auto audio = audio_table(o);
    if (auto r = write_rows(audio); !r)
        return r;

    last_mode_.reset();
    return {};
}

Ex<void> Adv7513Bus::configure_audio(const InitOptions& o) {
    return write_rows(audio_table(o));
}

Ex<void> Adv7513Bus::tmds_power(bool on) {
    const RegWrite w = power(on);
    return write_rows({&w, 1});
}

Ex<LinkStatus> Adv7513Bus::read_status() {
    auto r = a_.read(kMainAddr, kRegStatus);
    if (!r)
        return err(r.error().code, ERR_SITE(), kRegStatus.errno_detail(r.error()));
    return LinkStatus{*r};
}

Ex<void> Adv7513Bus::arm_interrupts(bool on) {
    const RegWrite rows[2] = {
        {kRegIntStatus, 0xFF},
        (on ? IntBits::hotplug() : IntBits{}).write_row(kRegIntEnable),
    };
    return write_rows(rows);
}

Ex<IntBits> Adv7513Bus::read_interrupts() {
    auto r = a_.read(kMainAddr, kRegIntStatus);
    if (!r)
        return err(r.error().code, ERR_SITE(), kRegIntStatus.errno_detail(r.error()));
    return IntBits{*r};
}

Ex<void> Adv7513Bus::clear_interrupts(IntBits pending) {
    const RegWrite w = pending.write_row(kRegIntStatus);
    return write_rows({&w, 1});
}

namespace {

Adv7513Bus::ModeBytes mode_bytes(std::span<const RegWrite> table) {
    Adv7513Bus::ModeBytes b{};
    for (const RegWrite& w : table) {
        if (w.reg == kRegVideoFormat) {
            b.sync_invert = w.val;
        } else if (w.reg == kRegPixelRepeat) {
            b.pr_flags = w.val;
        } else if (w.reg == kRegVic) {
            b.vic = w.val;
        }
    }
    return b;
}

}

Ex<void> Adv7513Bus::set_mode(const Modeline& m, bool direct_video_menu) {

    if constexpr (!kAdvModeWrite) {
        (void)m;
        (void)direct_video_menu;
        return {};
    } else {

        const std::array<RegWrite, 3> table = mode_table(m, direct_video_menu);
        const ModeBytes b = mode_bytes(table);

        if (last_mode_ == b)
            return {};
        for (const RegWrite& w : table) {
            auto r = a_.write(kMainAddr, w.reg, w.val);
            if (!r) {
                ++errors_;
                if (!fail_reg_)
                    fail_reg_ = w.reg;

                return err(r.error().code, ERR_SITE(), w.reg.errno_detail(r.error()));
            }
            ++writes_;
        }
        last_mode_ = b;
        return {};
    }
}

SpdPacket build_standard_spd(std::string_view core_name) {
    SpdPacket p{};

    p.b[0] = 0x83;
    p.b[1] = 0x01;
    p.b[2] = 25;
    p.b[3] = 0x00;

    static constexpr char kVendor[] = {'M', 'i', 'S', 'T', 'e', 'r'};
    for (std::size_t i = 0; i < sizeof kVendor; ++i) {
        p.b[4 + i] = static_cast<std::uint8_t>(kVendor[i]);
    }

    for (std::size_t i = 0; i < 16 && i < core_name.size(); ++i) {
        const char c = core_name[i];
        if (c == '\0')
            break;
        p.b[12 + i] = static_cast<std::uint8_t>(c);
    }

    p.b[28] = 0x08;
    return p;
}

Ex<void> Adv7513Bus::set_packet(
    std::uint8_t mask,
    os::I2cReg first_reg,
    std::optional<std::span<const std::uint8_t>> payload
) {

    constexpr std::size_t kPacketUpdateOff = 0x1F;
    if (payload && (payload->empty() || payload->size() > kPacketUpdateOff)) {
        return err(Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(payload->size()));
    }

    auto cur = a_.read(kMainAddr, kRegPacketEnable);
    if (!cur) {
        ++errors_;
        if (!spd_fail_reg_)
            spd_fail_reg_ = kRegPacketEnable;
        return err(cur.error().code, ERR_SITE(), kRegPacketEnable.errno_detail(cur.error()));
    }
    const std::uint8_t want = payload
        ? static_cast<std::uint8_t>(*cur | mask)
        : static_cast<std::uint8_t>(*cur & static_cast<std::uint8_t>(~mask));
    if (auto w = a_.write(kMainAddr, kRegPacketEnable, want); !w) {
        ++errors_;
        if (!spd_fail_reg_)
            spd_fail_reg_ = kRegPacketEnable;
        return err(w.error().code, ERR_SITE(), kRegPacketEnable.errno_detail(w.error()));
    }
    ++writes_;

    if (!payload)
        return {};

    const os::I2cReg upd = reg_after(first_reg, kPacketUpdateOff);
    if (auto w = a_.write(kSpdAddr, upd, 0x80); !w) {
        ++errors_;
        if (!spd_fail_reg_)
            spd_fail_reg_ = upd;
        return err(w.error().code, ERR_SITE(), upd.errno_detail(w.error()));
    }
    ++writes_;
    for (std::size_t i = 0; i < payload->size(); ++i) {
        const os::I2cReg reg = reg_after(first_reg, i);
        if (auto w = a_.write(kSpdAddr, reg, (*payload)[i]); !w) {

            ++errors_;
            if (!spd_fail_reg_)
                spd_fail_reg_ = reg;
            return err(w.error().code, ERR_SITE(), reg.errno_detail(w.error()));
        }
        ++writes_;
    }
    if (auto w = a_.write(kSpdAddr, upd, 0x00); !w) {
        ++errors_;
        if (!spd_fail_reg_)
            spd_fail_reg_ = upd;
        return err(w.error().code, ERR_SITE(), upd.errno_detail(w.error()));
    }
    ++writes_;
    return {};
}

Ex<void> Adv7513Bus::spd_config(const SpdPacket& p) {
    return set_packet(kPacketMaskSpd, kPacketOffsetSpd, std::span<const std::uint8_t>(p.b));
}

Ex<void> Adv7513Bus::spd_disable() {
    return set_packet(kPacketMaskSpd, kPacketOffsetSpd, std::nullopt);
}

Ex<Adv7513Bus::Verify> Adv7513Bus::verify(const Modeline& m, bool direct_video_menu) {
    Verify v{};
    const os::I2cReg regs[4] = {kRegPower, kRegVideoFormat, kRegPixelRepeat, kRegVic};
    std::uint8_t* const out[4] = {&v.r41, &v.r17, &v.r3b, &v.r3c};
    for (int i = 0; i < 4; ++i) {
        auto r = a_.read(kMainAddr, regs[i]);
        if (!r)
            return err(r.error().code, ERR_SITE(), regs[i].errno_detail(r.error()));
        *out[i] = *r;
    }

    const ModeBytes b = mode_bytes(mode_table(m, direct_video_menu));

    v.matched = (v.r41 == 0x10)
        && (!kAdvModeWrite || (v.r17 == b.sync_invert && v.r3b == b.pr_flags && v.r3c == b.vic));
    return v;
}

}
