// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/video_service.h"

#include "hal/selected.h"

#include "svc/filter_resend.h"
#include "svc/geometry_gate.h"
#include "svc/scaling_words.h"

#include "video_service_detail.h"

namespace mister::svc {

[[nodiscard]] Ex<void> emit_video_words(
    hal::ISpiTransport& link,
    std::uint8_t opcode,
    std::span<const std::uint16_t> words
) {
    if (words.empty() || words.size() > kVideoWireWords) {
        return err(Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(words.size()));
    }

    hal::Selected cs(link, hal::ChipSelect::Io);
    if (auto r = link.transfer(hal::SpiWord{opcode}); !r) {
        return std::unexpected(r.error());
    }
    for (const std::uint16_t w : words) {
        if (auto r = link.transfer(hal::SpiWord{w}); !r) {
            return std::unexpected(r.error());
        }
    }
    return {};
}

namespace {

[[nodiscard]] inline Ex<std::uint16_t> read_word(hal::ISpiTransport& link) {
    auto r = link.transfer(hal::SpiWord{0});
    if (!r)
        return std::unexpected(r.error());
    return r->v;
}

[[nodiscard]] inline Ex<std::uint32_t> read_dword(hal::ISpiTransport& link) {
    auto lo = read_word(link);
    if (!lo)
        return std::unexpected(lo.error());
    auto hi = read_word(link);
    if (!hi)
        return std::unexpected(hi.error());
    return static_cast<std::uint32_t>(*lo) | (static_cast<std::uint32_t>(*hi) << 16);
}

[[nodiscard]] inline Ex<void> write_bytes(
    hal::ISpiTransport& link,
    std::span<const std::uint8_t> b
) {
    for (const std::uint8_t byte : b) {
        auto r = link.transfer(hal::SpiWord{byte});
        if (!r)
            return std::unexpected(r.error());
    }
    return {};
}

}

[[nodiscard]] Ex<std::uint16_t> emit_fbuf(hal::ISpiTransport& link, std::uint16_t word) {
    hal::Selected cs(link, hal::ChipSelect::Io);

    auto op = link.transfer(hal::SpiWord{kUioSetFbuf});
    if (!op)
        return std::unexpected(op.error());
    if (auto r = link.transfer(hal::SpiWord{word}); !r) {
        return std::unexpected(r.error());
    }

    return op->v;
}

[[nodiscard]] Ex<void> emit_ar_custom(
    hal::ISpiTransport& link,
    std::span<const std::uint16_t> words
) {
    if (words.size() != 4) {
        return err(Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(words.size()));
    }
    hal::Selected cs(link, hal::ChipSelect::Io);
    if (auto r = link.transfer(hal::SpiWord{kUioSetArCust}); !r) {
        return std::unexpected(r.error());
    }
    for (const std::uint16_t w : words) {
        if (auto r = link.transfer(hal::SpiWord{w}); !r) {
            return std::unexpected(r.error());
        }
    }
    return {};
}

[[nodiscard]] Ex<std::uint16_t> emit_filter_mode(hal::ISpiTransport& link, std::uint8_t mode) {
    hal::Selected cs(link, hal::ChipSelect::Io);
    auto op = link.transfer(hal::SpiWord{kUioSetFltNum});
    if (!op)
        return std::unexpected(op.error());

    if (op->v == 0)
        return std::uint16_t{0};

    const std::uint8_t b[1] = {mode};
    if (auto r = write_bytes(link, std::span<const std::uint8_t>(b, 1)); !r) {
        return std::unexpected(r.error());
    }
    return op->v;
}

[[nodiscard]] Ex<void> emit_filter_coeff_chunk(
    hal::ISpiTransport& link,
    std::span<const std::uint16_t> chunk
) {

    if (chunk.size() > kFilterCoeffWords) {
        return err(Errc::slot_range, ERR_SITE(), static_cast<std::uint32_t>(chunk.size()));
    }
    hal::Selected cs(link, hal::ChipSelect::Io);
    if (auto r = link.transfer(hal::SpiWord{kUioSetFltCoef}); !r) {
        return std::unexpected(r.error());
    }
    for (const std::uint16_t w : chunk) {
        if (auto r = link.transfer(hal::SpiWord{w}); !r) {
            return std::unexpected(r.error());
        }
    }
    return {};
}

[[nodiscard]] Ex<std::uint16_t> emit_shadow_mask(
    hal::ISpiTransport& link,
    std::uint16_t flag_word
) {
    hal::Selected cs(link, hal::ChipSelect::Io);
    auto op = link.transfer(hal::SpiWord{kUioShadowMask});
    if (!op)
        return std::unexpected(op.error());

    if (op->v == 0)
        return std::uint16_t{0};
    if (auto r = link.transfer(hal::SpiWord{flag_word}); !r) {
        return std::unexpected(r.error());
    }

    if (auto r = link.transfer(hal::SpiWord{0}); !r) {
        return std::unexpected(r.error());
    }
    return op->v;
}

[[nodiscard]] Ex<std::uint16_t> emit_hdmi_int_probe(hal::ISpiTransport& link) {
    hal::Selected cs(link, hal::ChipSelect::Io);
    auto op = link.transfer(hal::SpiWord{kUioHdmiInt});
    if (!op)
        return std::unexpected(op.error());
    return op->v;
}

[[nodiscard]] Ex<std::uint16_t> emit_gamma_probe(hal::ISpiTransport& link) {
    hal::Selected cs(link, hal::ChipSelect::Io);
    auto op = link.transfer(hal::SpiWord{kUioSetGamma});
    if (!op)
        return std::unexpected(op.error());
    return op->v;
}

[[nodiscard]] Ex<GeometryPass> sample_video_geometry(
    hal::ISpiTransport& link,
    bool force,
    GeometryGate& gate,
    IFilterResend* filters
) {
    GeometryPass pass{};
    pass.sample = gate.last;

    std::uint16_t commit_res = gate.last_res;
    std::uint8_t commit_fb_crc = gate.last_fb_crc;
    std::uint16_t commit_flt = gate.last_flt_flags;
    bool commit_vi_seen = gate.vi_seen;

    {
        hal::Selected cs(link, hal::ChipSelect::Io);

        if (auto r = link.transfer(hal::SpiWord{kUioGetVres}); !r) {
            return std::unexpected(r.error());
        }
        auto res = read_word(link);
        if (!res)
            return std::unexpected(res.error());
        pass.res_changed = (*res != gate.last_res);
        pass.sample.res = *res;

        commit_res = *res;
        if (pass.res_changed || force) {
            if (pass.res_changed)
                commit_vi_seen = true;

            auto width = read_dword(link);
            if (!width)
                return std::unexpected(width.error());
            pass.sample.width = *width;
            auto height = read_dword(link);
            if (!height)
                return std::unexpected(height.error());
            pass.sample.height = *height;
            auto htime = read_dword(link);
            if (!htime)
                return std::unexpected(htime.error());
            pass.sample.htime = *htime;
            auto vtime = read_dword(link);
            if (!vtime)
                return std::unexpected(vtime.error());
            pass.sample.vtime = *vtime;
            auto ptime = read_dword(link);
            if (!ptime)
                return std::unexpected(ptime.error());
            pass.sample.ptime = *ptime;
            auto vtimeh = read_dword(link);
            if (!vtimeh)
                return std::unexpected(vtimeh.error());
            pass.sample.vtimeh = *vtimeh;
            auto ctime = read_dword(link);
            if (!ctime)
                return std::unexpected(ctime.error());
            pass.sample.ctime = *ctime;
            auto pixrep = read_word(link);
            if (!pixrep)
                return std::unexpected(pixrep.error());
            pass.sample.pixrep = *pixrep;
            auto de_h = read_word(link);
            if (!de_h)
                return std::unexpected(de_h.error());
            pass.sample.de_h = *de_h;
            auto de_v = read_word(link);
            if (!de_v)
                return std::unexpected(de_v.error());
            pass.sample.de_v = *de_v;

            auto fc_lo = read_word(link);
            if (!fc_lo)
                return std::unexpected(fc_lo.error());
            auto fc_hi = read_word(link);
            if (!fc_hi)
                return std::unexpected(fc_hi.error());
            pass.sample.frame_clocks = static_cast<std::uint32_t>(*fc_lo)
                | ((static_cast<std::uint32_t>(*fc_hi) & 0xFFu) << 16);
        }
    }

    {
        hal::Selected cs(link, hal::ChipSelect::Io);

        auto op = link.transfer(hal::SpiWord{kUioGetFbPar});
        if (!op)
            return std::unexpected(op.error());
        const auto crc = static_cast<std::uint8_t>(op->v & 0xFFu);
        pass.fb_changed = (crc != gate.last_fb_crc);
        commit_fb_crc = crc;
        pass.sample.fb_crc = crc;
        if (pass.fb_changed || force || pass.res_changed) {
            auto arx_raw = read_word(link);
            if (!arx_raw)
                return std::unexpected(arx_raw.error());
            pass.sample.arx_raw = *arx_raw;
            auto ary_raw = read_word(link);
            if (!ary_raw)
                return std::unexpected(ary_raw.error());
            pass.sample.ary_raw = *ary_raw;
            auto fb_fmt = read_word(link);
            if (!fb_fmt)
                return std::unexpected(fb_fmt.error());
            pass.sample.fb_fmt = *fb_fmt;
            auto fb_width = read_word(link);
            if (!fb_width)
                return std::unexpected(fb_width.error());
            pass.sample.fb_width = *fb_width;
            auto fb_height = read_word(link);
            if (!fb_height)
                return std::unexpected(fb_height.error());
            pass.sample.fb_height = *fb_height;
        }
    }

    pass.changed = commit_vi_seen && (pass.res_changed || pass.fb_changed);

    if (pass.changed || force) {
        hal::Selected cs(link, hal::ChipSelect::Io);
        const std::uint8_t b[2] = {kUioSetYcPar, 0x00};
        if (auto r = write_bytes(link, std::span<const std::uint8_t>(b, 2)); !r) {
            return std::unexpected(r.error());
        }
        pass.yc_sent = true;
    }

    const bool resend = pass.changed && !gate.policy.front_end;
    bool vfilter_gate_open = false;
    {
        hal::Selected cs(link, hal::ChipSelect::Io);
        auto op = link.transfer(hal::SpiWord{kUioSetFltNum});
        if (!op)
            return std::unexpected(op.error());
        pass.sample.flt_flags = op->v;
        if (op->v != 0 && (resend || gate.last_flt_flags != op->v)) {
            commit_flt = op->v;
            const std::uint8_t mode[1] = {gate.policy.filter_mode};
            if (auto r = write_bytes(link, std::span<const std::uint8_t>(mode, 1)); !r) {
                return std::unexpected(r.error());
            }
            pass.resent = true;
            vfilter_gate_open = true;
        }
    }

    bool tail_deferred = false;
    if (vfilter_gate_open && filters != nullptr) {
        tail_deferred = filters->plan_filter_send(pass.sample, resend);
    }

    if (vfilter_gate_open && !tail_deferred) {
        hal::Selected cs(link, hal::ChipSelect::Io);
        if (auto r = link.transfer(hal::SpiWord{kUioSetFltCoef}); !r) {
            return std::unexpected(r.error());
        }
    }

    if (resend && !tail_deferred) {

        const ScalingWords sw = scaling_words(pass.sample, gate.policy);
        const std::uint16_t hpay = sw.height;
        const std::uint16_t wpay = sw.width;

        {
            hal::Selected cs(link, hal::ChipSelect::Io);
            if (auto r = link.transfer(hal::SpiWord{kUioSetHeight}); !r) {
                return std::unexpected(r.error());
            }
            if (auto r = link.transfer(hal::SpiWord{hpay}); !r) {
                return std::unexpected(r.error());
            }
        }
        {
            hal::Selected cs(link, hal::ChipSelect::Io);
            if (auto r = link.transfer(hal::SpiWord{kUioSetWidth}); !r) {
                return std::unexpected(r.error());
            }
            if (auto r = link.transfer(hal::SpiWord{wpay}); !r) {
                return std::unexpected(r.error());
            }
        }
    }

    gate.last_res = commit_res;
    gate.last_fb_crc = commit_fb_crc;
    gate.last_flt_flags = commit_flt;
    gate.vi_seen = commit_vi_seen;
    gate.last = pass.sample;
    return pass;
}

}
