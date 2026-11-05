// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/probe_ops.h"

#include "svc/filter_resend.h"
#include "svc/geometry_gate.h"
#include "svc/scaling_words.h"

#include "video_service_detail.h"

namespace mister::svc {

Ex<std::uint16_t> ProbeOps::emit_filter_mode(std::uint8_t mode) {
    auto op = link_.command(proto::UioOp::SetFltNum);
    if (!op)
        return std::unexpected(op.error());

    if (*op == 0) {
        link_.close();
        return std::uint16_t{0};
    }

    if (auto r = link_.write_byte(mode); !r) {
        return std::unexpected(r.error());
    }
    link_.close();
    return *op;
}

Ex<std::uint16_t> ProbeOps::emit_shadow_mask(std::uint16_t flag_word) {
    auto op = link_.command(proto::UioOp::ShadowMask);
    if (!op)
        return std::unexpected(op.error());

    if (*op == 0) {
        link_.close();
        return std::uint16_t{0};
    }
    if (auto r = link_.word(flag_word); !r) {
        return std::unexpected(r.error());
    }

    if (auto r = link_.word(0); !r) {
        return std::unexpected(r.error());
    }
    link_.close();
    return *op;
}

Ex<std::uint16_t> ProbeOps::emit_hdmi_int_probe() {
    auto op = link_.command(proto::UioOp::HdmiInt);
    if (!op)
        return std::unexpected(op.error());
    link_.close();
    return *op;
}

Ex<std::uint16_t> ProbeOps::emit_gamma_probe() {
    auto op = link_.command(proto::UioOp::SetGamma);
    if (!op)
        return std::unexpected(op.error());
    link_.close();
    return *op;
}

Ex<GeometryPass> ProbeOps::sample_video_geometry(
    bool force,
    GeometryGate& gate,
    IFilterResend& filters
) {
    GeometryPass pass{};
    pass.sample = gate.last;

    std::uint16_t commit_res = gate.last_res;
    std::uint8_t commit_fb_crc = gate.last_fb_crc;
    std::uint16_t commit_flt = gate.last_flt_flags;
    bool commit_vi_seen = gate.vi_seen;

    {

        if (auto op = link_.command(proto::UioOp::GetVres); !op) {
            return std::unexpected(op.error());
        }
        auto res = link_.read();
        if (!res)
            return std::unexpected(res.error());
        pass.res_changed = (*res != gate.last_res);
        pass.sample.res = *res;

        commit_res = *res;
        if (pass.res_changed || force) {
            if (pass.res_changed)
                commit_vi_seen = true;

            auto width = link_.read_dword();
            if (!width)
                return std::unexpected(width.error());
            pass.sample.active.width = *width;
            auto height = link_.read_dword();
            if (!height)
                return std::unexpected(height.error());
            pass.sample.active.height = *height;
            auto htime = link_.read_dword();
            if (!htime)
                return std::unexpected(htime.error());
            pass.sample.htime = *htime;
            auto vtime = link_.read_dword();
            if (!vtime)
                return std::unexpected(vtime.error());
            pass.sample.vtime = *vtime;
            auto ptime = link_.read_dword();
            if (!ptime)
                return std::unexpected(ptime.error());
            pass.sample.ptime = *ptime;
            auto vtimeh = link_.read_dword();
            if (!vtimeh)
                return std::unexpected(vtimeh.error());
            pass.sample.vtimeh = *vtimeh;
            auto ctime = link_.read_dword();
            if (!ctime)
                return std::unexpected(ctime.error());
            pass.sample.ctime = *ctime;
            auto pixrep = link_.read();
            if (!pixrep)
                return std::unexpected(pixrep.error());
            pass.sample.pixrep = *pixrep;
            auto de_h = link_.read();
            if (!de_h)
                return std::unexpected(de_h.error());
            pass.sample.de_h = *de_h;
            auto de_v = link_.read();
            if (!de_v)
                return std::unexpected(de_v.error());
            pass.sample.de_v = *de_v;

            auto fc_lo = link_.read();
            if (!fc_lo)
                return std::unexpected(fc_lo.error());
            auto fc_hi = link_.read();
            if (!fc_hi)
                return std::unexpected(fc_hi.error());
            pass.sample.frame_clocks = static_cast<std::uint32_t>(*fc_lo)
                | ((static_cast<std::uint32_t>(*fc_hi) & 0xFFu) << 16);
        }
    }

    {

        auto op = link_.command(proto::UioOp::GetFbPar);
        if (!op)
            return std::unexpected(op.error());
        const auto crc = static_cast<std::uint8_t>(*op & 0xFFu);
        pass.fb_changed = (crc != gate.last_fb_crc);
        commit_fb_crc = crc;
        pass.sample.fb_crc = crc;
        if (pass.fb_changed || force || pass.res_changed) {
            auto arx_raw = link_.read();
            if (!arx_raw)
                return std::unexpected(arx_raw.error());
            pass.sample.arx_raw = *arx_raw;
            auto ary_raw = link_.read();
            if (!ary_raw)
                return std::unexpected(ary_raw.error());
            pass.sample.ary_raw = *ary_raw;
            auto fb_fmt = link_.read();
            if (!fb_fmt)
                return std::unexpected(fb_fmt.error());
            pass.sample.fb_fmt = *fb_fmt;
            auto fb_width = link_.read();
            if (!fb_width)
                return std::unexpected(fb_width.error());
            pass.sample.fb_width = *fb_width;
            auto fb_height = link_.read();
            if (!fb_height)
                return std::unexpected(fb_height.error());
            pass.sample.fb_height = *fb_height;
        }
    }

    pass.changed = commit_vi_seen && (pass.res_changed || pass.fb_changed);

    if (pass.changed || force) {

        if (auto r = link_.command(hal::ChipSelect::Io, proto::byte(proto::UioOp::SetYcPar)); !r) {
            return std::unexpected(r.error());
        }
        if (auto r = link_.write_byte(0x00); !r) {
            return std::unexpected(r.error());
        }
        pass.yc_sent = true;
    }

    const bool resend = pass.changed && !gate.policy.front_end;
    bool vfilter_gate_open = false;
    {
        auto op = link_.command(proto::UioOp::SetFltNum);
        if (!op)
            return std::unexpected(op.error());
        pass.sample.flt_flags = *op;
        if (*op != 0 && (resend || gate.last_flt_flags != *op)) {
            commit_flt = *op;
            if (auto r = link_.write_byte(gate.policy.filter_mode); !r) {
                return std::unexpected(r.error());
            }
            pass.resent = true;
            vfilter_gate_open = true;
        }
    }

    bool tail_deferred = false;
    if (vfilter_gate_open) {
        tail_deferred = filters.plan_filter_send(pass.sample, resend);
    }

    if (vfilter_gate_open && !tail_deferred) {
        if (auto r = link_.command(proto::UioOp::SetFltCoef); !r) {
            return std::unexpected(r.error());
        }
    }

    if (resend && !tail_deferred) {

        const ScalingWords sw = scaling_words(pass.sample, gate.policy);
        const std::uint16_t hpay = sw.height;
        const std::uint16_t wpay = sw.width;

        {
            if (auto r = link_.command(proto::UioOp::SetHeight); !r) {
                return std::unexpected(r.error());
            }
            if (auto r = link_.word(hpay); !r) {
                return std::unexpected(r.error());
            }
        }
        {
            if (auto r = link_.command(proto::UioOp::SetWidth); !r) {
                return std::unexpected(r.error());
            }
            if (auto r = link_.word(wpay); !r) {
                return std::unexpected(r.error());
            }
        }
    }

    gate.last_res = commit_res;
    gate.last_fb_crc = commit_fb_crc;
    gate.last_flt_flags = commit_flt;
    gate.vi_seen = commit_vi_seen;
    gate.last = pass.sample;
    link_.close();
    return pass;
}

Ex<std::uint16_t> ProbeOps::emit_afilter_probe() {

    auto op = link_.command(proto::UioOp::SetAfilter);
    if (!op)
        return std::unexpected(op.error());
    link_.close();
    return *op;
}

}
