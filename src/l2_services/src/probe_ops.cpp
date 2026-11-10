// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/probe_ops.h"

#include "svc/filter_resend.h"
#include "svc/filter_store.h"
#include "svc/geometry_gate.h"
#include "svc/scaling_words.h"

namespace mister::svc {

Ex<std::uint16_t> ProbeOps::emit_filter_mode(ScalerSlotMode mode) {
    auto op = link_.command(proto::UioOp::SetFltNum);
    if (!op)
        return std::unexpected(op.error());

    if (*op == 0) {
        link_.close();
        return std::uint16_t{0};
    }

    if (auto r = mode.write_to(link_); !r) {
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

    const auto vres = read_vres_(gate, force, pass);
    if (!vres)
        return std::unexpected(vres.error());
    const auto fb = read_fb_par_(gate, force, vres->res_changed, pass);
    if (!fb)
        return std::unexpected(fb.error());
    pass.changed = vres->vi_seen && (vres->res_changed || fb->fb_changed);

    if (auto r = send_yc_(force, pass); !r)
        return std::unexpected(r.error());
    const bool resend = pass.changed && !gate.policy.front_end;
    const auto flt = send_vfilter_(gate, resend, filters, pass);
    if (!flt)
        return std::unexpected(flt.error());
    if (resend && !flt->tail_deferred) {
        if (auto r = send_scaling_(gate.policy, pass.sample); !r)
            return std::unexpected(r.error());
    }

    gate.last_res = vres->res;
    gate.last_fb_crc = fb->crc;
    gate.last_flt_flags = flt->flags;
    gate.vi_seen = vres->vi_seen;
    gate.last = pass.sample;
    link_.close();
    return pass;
}

Ex<ProbeOps::VresStep> ProbeOps::read_vres_(
    const GeometryGate& gate,
    bool force,
    GeometryPass& pass
) {
    VresStep out{};
    out.vi_seen = gate.vi_seen;
    if (auto op = link_.command(proto::UioOp::GetVres); !op) {
        return std::unexpected(op.error());
    }
    auto res = link_.read();
    if (!res)
        return std::unexpected(res.error());
    out.res = ResWord{*res};
    out.res_changed = (out.res != gate.last_res);
    pass.res_changed = out.res_changed;
    pass.sample.res = out.res;
    if (!out.res_changed && !force)
        return out;
    if (out.res_changed)
        out.vi_seen = true;
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
    return out;
}

Ex<ProbeOps::FbParStep> ProbeOps::read_fb_par_(
    const GeometryGate& gate,
    bool force,
    bool res_changed,
    GeometryPass& pass
) {
    FbParStep out{};
    auto op = link_.command(proto::UioOp::GetFbPar);
    if (!op)
        return std::unexpected(op.error());
    out.crc = FbParCrc::of(*op);
    out.fb_changed = (out.crc != gate.last_fb_crc);
    pass.fb_changed = out.fb_changed;
    pass.sample.fb_crc = out.crc;
    if (!out.fb_changed && !force && !res_changed)
        return out;
    auto arx_raw = link_.read();
    if (!arx_raw)
        return std::unexpected(arx_raw.error());
    pass.sample.aspect.arx_raw = *arx_raw;
    auto ary_raw = link_.read();
    if (!ary_raw)
        return std::unexpected(ary_raw.error());
    pass.sample.aspect.ary_raw = *ary_raw;
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
    return out;
}

Ex<void> ProbeOps::send_yc_(bool force, GeometryPass& pass) {
    if (!pass.changed && !force)
        return {};

    if (auto r = link_.command(hal::ChipSelect::Io, proto::byte(proto::UioOp::SetYcPar)); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = link_.write_byte(0x00); !r) {
        return std::unexpected(r.error());
    }
    pass.yc_sent = true;
    return {};
}

Ex<ProbeOps::VfilterStep> ProbeOps::send_vfilter_(
    const GeometryGate& gate,
    bool resend,
    IFilterResend& filters,
    GeometryPass& pass
) {
    VfilterStep out{};
    out.flags = gate.last_flt_flags;
    auto op = link_.command(proto::UioOp::SetFltNum);
    if (!op)
        return std::unexpected(op.error());
    const FltFlags flt{*op};
    pass.sample.flt_flags = flt;
    bool gate_open = false;
    if (flt.present() && (resend || gate.last_flt_flags != flt)) {
        out.flags = flt;
        if (auto r = gate.policy.filter_mode.write_to(link_); !r) {
            return std::unexpected(r.error());
        }
        pass.resent = true;
        gate_open = true;
    }

    if (gate_open)
        out.tail_deferred = filters.plan_filter_send(pass.sample, resend);

    if (gate_open && !out.tail_deferred) {
        if (auto r = link_.command(proto::UioOp::SetFltCoef); !r) {
            return std::unexpected(r.error());
        }
    }
    return out;
}

Ex<void> ProbeOps::send_scaling_(const ScalingPolicy& policy, const VideoSample& sample) {
    const ScalingWords sw = scaling_words(sample, policy);

    if (auto r = link_.command(proto::UioOp::SetHeight); !r)
        return std::unexpected(r.error());
    if (auto r = link_.word(sw.height); !r)
        return std::unexpected(r.error());
    if (auto r = link_.command(proto::UioOp::SetWidth); !r)
        return std::unexpected(r.error());

    return link_.word(sw.width);
}

Ex<std::uint16_t> ProbeOps::emit_afilter_probe() {

    auto op = link_.command(proto::UioOp::SetAfilter);
    if (!op)
        return std::unexpected(op.error());
    link_.close();
    return *op;
}

}
