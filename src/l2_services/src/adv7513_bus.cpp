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

namespace mister::svc::adv7513 {

namespace {

std::unexpected<Error> err(Errc c, std::uint16_t site, std::uint32_t detail = 0) {
    return std::unexpected(Error{c, site, detail});
}

}  // namespace

LinuxAdapter::Slot* LinuxAdapter::find(std::uint8_t addr) noexcept {
    for (Slot& s : slots_) {
        if (s.dev && s.addr == addr) return &s;
    }
    return nullptr;
}

Ex<std::uint8_t> LinuxAdapter::probe(unsigned bus, std::uint8_t addr) {
    TASTY_SEAT_BODY(LinuxAdapter);

    auto dev = os::I2cDevice::open(bus, addr);
    if (!dev) return std::unexpected(dev.error());
    return dev->read_byte();
}

Ex<void> LinuxAdapter::open(unsigned bus, std::uint8_t addr) {
    TASTY_SEAT_BODY(LinuxAdapter);
    if (find(addr) != nullptr) return {};
    for (Slot& s : slots_) {
        if (s.dev) continue;
        auto dev = os::I2cDevice::open(bus, addr);
        if (!dev) return std::unexpected(dev.error());
        s.addr = addr;
        s.dev.emplace(std::move(*dev));
        return {};
    }

    return err(Errc::bad_format, ERR_SITE(), addr);
}

Ex<std::uint8_t> LinuxAdapter::read(std::uint8_t addr, std::uint8_t reg) {
    TASTY_SEAT_BODY(LinuxAdapter);
    Slot* s = find(addr);
    if (s == nullptr) return err(Errc::not_found, ERR_SITE(), addr);
    return s->dev->read_reg(reg);
}

Ex<void> LinuxAdapter::write(std::uint8_t addr, std::uint8_t reg, std::uint8_t val) {
    TASTY_SEAT_BODY(LinuxAdapter);
    Slot* s = find(addr);
    if (s == nullptr) return err(Errc::not_found, ERR_SITE(), addr);
    return s->dev->write_reg(reg, val);
}

Ex<Adv7513Bus> Adv7513Bus::attach(II2cAdapter& a, const hal::VideoOutDecl& board,
                                  ProbeReport& out) {
    out = ProbeReport{};

    const auto range = board.i2c_buses.resolve(
        ERR_SITE(), static_cast<std::uint32_t>(hal::FactField::VideoI2cBuses));
    if (!range) {
        out.presence = Presence::Absent;
        out.bus = 0xFF;
        return std::unexpected(range.error());
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
        out.bus = 0xFF;
        return err(Errc::not_found, ERR_SITE(), kMainAddr);
    }
    if (out.acks >= 2) {

        out.presence = Presence::Ambiguous;
        return err(Errc::bad_format, ERR_SITE(), out.acks);
    }

    if (auto o = a.open(found, kMainAddr); !o) {
        out.presence = Presence::Rejected;
        return std::unexpected(o.error());
    }

    auto r42 = a.read(kMainAddr, kRegStatus);
    if (!r42) {
        out.presence = Presence::Rejected;
        return std::unexpected(r42.error());
    }
    auto r41 = a.read(kMainAddr, kRegPower);
    if (!r41) {
        out.presence = Presence::Rejected;
        return std::unexpected(r41.error());
    }
    out.r42 = *r42;
    out.r41 = *r41;

    if ((out.r41 == 0xFF && out.r42 == 0xFF) || (out.r41 == 0x00 && out.r42 == 0x00)) {
        out.presence = Presence::Rejected;
        return err(Errc::bad_format, ERR_SITE(),
                   static_cast<std::uint32_t>(out.r41) |
                       (static_cast<std::uint32_t>(out.r42) << 8));
    }

    const struct {
        std::uint8_t addr;
        bool* ack;
    } subs[3] = {
        {kEdidAddr, &out.edid_ack},
        {kSpdAddr, &out.spd_ack},
        {kCecAddr, &out.cec_ack},
    };
    for (const auto& s : subs) {
        if (a.probe(found, s.addr)) {
            *s.ack = a.open(found, s.addr).has_value();
        }
    }

    out.presence = Presence::Found;
    Adv7513Bus b;
    b.a_ = &a;
    b.bus_ = found;
    return b;
}

Ex<void> Adv7513Bus::write_rows(std::span<const RegWrite> rows) {
    for (const RegWrite& w : rows) {
        auto r = a_->write(kMainAddr, w.reg, w.val);
        if (!r) {

            ++errors_;
            if (fail_reg_ == 0) fail_reg_ = w.reg;
            return err(r.error().code, ERR_SITE(), reg_errno_detail(w.reg, r.error().detail));
        }
        ++writes_;
    }
    return {};
}

Ex<void> Adv7513Bus::configure(const InitOptions& o) {
    const auto table = init_table(o);
    if (auto r = write_rows(table); !r) return r;

    const auto audio = audio_table(o);
    if (auto r = write_rows(audio); !r) return r;

    last_sync_invert_ = 0xFF;
    last_pr_flags_ = 0xFF;
    last_vic_ = 0xFF;
    return {};
}

Ex<void> Adv7513Bus::configure_audio(const InitOptions& o) { return write_rows(audio_table(o)); }

Ex<void> Adv7513Bus::tmds_power(bool on) {
    const RegWrite w = power(on);
    return write_rows({&w, 1});
}

Ex<std::uint8_t> Adv7513Bus::read_status() {
    auto r = a_->read(kMainAddr, kRegStatus);
    if (!r) return err(r.error().code, ERR_SITE(), reg_errno_detail(kRegStatus, r.error().detail));
    return *r;
}

Ex<void> Adv7513Bus::arm_interrupts(bool on) {
    const RegWrite rows[2] = {
        {kRegIntStatus, 0xFF},
        {kRegIntEnable, static_cast<std::uint8_t>(on ? 0xC0 : 0x00)},
    };
    return write_rows(rows);
}

namespace {

struct ModeBytes {
    std::uint8_t sync_invert, pr_flags, vic;
};

ModeBytes mode_bytes(const Modeline& m, bool direct_video_menu) {
    const auto t = mode_table(m, direct_video_menu);
    return ModeBytes{t[0].val, t[1].val, t[2].val};
}

}  // namespace

Ex<void> Adv7513Bus::set_mode(const Modeline& m, bool direct_video_menu) {

    if constexpr (!kAdvModeWrite) {
        (void)m;
        (void)direct_video_menu;
        return {};
    } else {
        const ModeBytes b = mode_bytes(m, direct_video_menu);

        if (last_sync_invert_ == b.sync_invert && last_pr_flags_ == b.pr_flags &&
            last_vic_ == b.vic) {
            return {};
        }
        const RegWrite rows[3] = {
            {0x17, b.sync_invert},
            {0x3B, b.pr_flags},
            {0x3C, b.vic},
        };
        for (const RegWrite& w : rows) {
            auto r = a_->write(kMainAddr, w.reg, w.val);
            if (!r) {
                ++errors_;
                if (fail_reg_ == 0) fail_reg_ = w.reg;

                return err(r.error().code, ERR_SITE(), reg_errno_detail(w.reg, r.error().detail));
            }
            ++writes_;
        }
        last_sync_invert_ = b.sync_invert;
        last_pr_flags_ = b.pr_flags;
        last_vic_ = b.vic;
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
        if (c == '\0') break;
        p.b[12 + i] = static_cast<std::uint8_t>(c);
    }

    p.b[28] = 0x08;
    return p;
}

Ex<void> Adv7513Bus::set_packet(std::uint8_t mask, std::uint8_t offset, const std::uint8_t* data,
                                std::size_t n) {

    if (data != nullptr && (n == 0 || n > kPacketUpdateOff)) {
        return err(Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(n));
    }

    auto cur = a_->read(kMainAddr, kRegPacketEnable);
    if (!cur) {
        ++errors_;
        if (spd_fail_reg_ == 0) spd_fail_reg_ = kRegPacketEnable;
        return err(cur.error().code, ERR_SITE(),
                   reg_errno_detail(kRegPacketEnable, cur.error().detail));
    }
    const std::uint8_t want =
        data != nullptr ? static_cast<std::uint8_t>(*cur | mask)
                        : static_cast<std::uint8_t>(*cur & static_cast<std::uint8_t>(~mask));
    if (auto w = a_->write(kMainAddr, kRegPacketEnable, want); !w) {
        ++errors_;
        if (spd_fail_reg_ == 0) spd_fail_reg_ = kRegPacketEnable;
        return err(w.error().code, ERR_SITE(),
                   reg_errno_detail(kRegPacketEnable, w.error().detail));
    }
    ++writes_;

    if (data == nullptr) return {};

    const std::uint8_t upd = static_cast<std::uint8_t>(offset + kPacketUpdateOff);
    if (auto w = a_->write(kSpdAddr, upd, 0x80); !w) {
        ++errors_;
        if (spd_fail_reg_ == 0) spd_fail_reg_ = upd;
        return err(w.error().code, ERR_SITE(), reg_errno_detail(upd, w.error().detail));
    }
    ++writes_;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t reg = static_cast<std::uint8_t>(offset + i);
        if (auto w = a_->write(kSpdAddr, reg, data[i]); !w) {

            ++errors_;
            if (spd_fail_reg_ == 0) spd_fail_reg_ = reg;
            return err(w.error().code, ERR_SITE(), reg_errno_detail(reg, w.error().detail));
        }
        ++writes_;
    }
    if (auto w = a_->write(kSpdAddr, upd, 0x00); !w) {
        ++errors_;
        if (spd_fail_reg_ == 0) spd_fail_reg_ = upd;
        return err(w.error().code, ERR_SITE(), reg_errno_detail(upd, w.error().detail));
    }
    ++writes_;
    return {};
}

Ex<void> Adv7513Bus::spd_config(const SpdPacket& p) {
    return set_packet(kPacketMaskSpd, kPacketOffsetSpd, p.b.data(), p.b.size());
}

Ex<void> Adv7513Bus::spd_disable() {
    return set_packet(kPacketMaskSpd, kPacketOffsetSpd, nullptr, 0);
}

Ex<Adv7513Bus::Verify> Adv7513Bus::verify(const Modeline& m, bool direct_video_menu) {
    Verify v{};
    const std::uint8_t regs[4] = {kRegPower, 0x17, 0x3B, 0x3C};
    std::uint8_t* const out[4] = {&v.r41, &v.r17, &v.r3b, &v.r3c};
    for (int i = 0; i < 4; ++i) {
        auto r = a_->read(kMainAddr, regs[i]);
        if (!r) return err(r.error().code, ERR_SITE(), reg_errno_detail(regs[i], r.error().detail));
        *out[i] = *r;
    }

    const ModeBytes b = mode_bytes(m, direct_video_menu);

    v.matched =
        (v.r41 == 0x10) &&
        (!kAdvModeWrite || (v.r17 == b.sync_invert && v.r3b == b.pr_flags && v.r3c == b.vic));
    return v;
}

}  // namespace mister::svc::adv7513
