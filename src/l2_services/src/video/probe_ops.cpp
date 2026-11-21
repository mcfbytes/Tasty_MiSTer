// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/video/probe_ops.h"

#include <array>

#include "infra/prelude.h"
#include "svc/video/filter_resend.h"
#include "svc/video/filter_store.h"
#include "svc/video/geometry_gate.h"
#include "svc/video/scaling_words.h"

namespace mister::svc::video {

Ex<hal::SpiWord> ProbeOps::emit_filter_mode(ScalerSlotMode mode) {
    auto op = link_.command(proto::UioOp::SetFltNum);
    if (!op)
        return std::unexpected(op.error());

    if (*op == hal::SpiWord{}) {
        link_.close();
        return hal::SpiWord{};
    }

    if (auto r = mode.write_to(link_); !r) {
        return std::unexpected(r.error());
    }
    link_.close();
    return *op;
}

Ex<void> ShadowMaskWord::write_to(proto::link::Link& link) const {
    return link.word(hal::SpiWord{static_cast<std::uint16_t>(v)});
}

Ex<hal::SpiWord> ProbeOps::emit_shadow_mask(ShadowMaskWord flag) {
    auto op = link_.command(proto::UioOp::ShadowMask);
    if (!op)
        return std::unexpected(op.error());

    if (*op == hal::SpiWord{}) {
        link_.close();
        return hal::SpiWord{};
    }
    if (auto r = flag.write_to(link_); !r) {
        return std::unexpected(r.error());
    }

    if (auto r = link_.word(hal::SpiWord{}); !r) {
        return std::unexpected(r.error());
    }
    link_.close();
    return *op;
}

Ex<hal::SpiWord> ProbeOps::emit_hdmi_int_probe() {
    auto op = link_.command(proto::UioOp::HdmiInt);
    if (!op)
        return std::unexpected(op.error());
    link_.close();
    return *op;
}

Ex<hal::SpiWord> ProbeOps::emit_gamma_probe() {
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
    out.res = ResWord::of(*res);
    out.res_changed = (out.res != gate.last_res);
    pass.res_changed = out.res_changed;
    pass.sample.res = out.res;
    if (!out.res_changed && !force)
        return out;
    if (out.res_changed)
        out.vi_seen = true;
    std::array<hal::SpiWord, VideoSample::kVresRunWords> run{};
    for (hal::SpiWord& w : run) {
        auto r = link_.read();
        if (!r)
            return std::unexpected(r.error());
        w = *r;
    }
    pass.sample = pass.sample.with_vres_run(run);
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
    std::array<hal::SpiWord, VideoSample::kFbParRunWords> run{};
    for (hal::SpiWord& w : run) {
        auto r = link_.read();
        if (!r)
            return std::unexpected(r.error());
        w = *r;
    }
    pass.sample = pass.sample.with_fb_par_run(run);
    return out;
}

Ex<void> ProbeOps::send_yc_(bool force, GeometryPass& pass) {
    if (!pass.changed && !force)
        return {};

    if (auto r = link_.command(
            proto::LinkSel::Io,
            hal::SpiWord{proto::byte(proto::UioOp::SetYcPar)}
        );
        !r) {
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
    const FltFlags flt = FltFlags::of(*op);
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

Ex<hal::SpiWord> ProbeOps::emit_afilter_probe() {

    auto op = link_.command(proto::UioOp::SetAfilter);
    if (!op)
        return std::unexpected(op.error());
    link_.close();
    return *op;
}

}
