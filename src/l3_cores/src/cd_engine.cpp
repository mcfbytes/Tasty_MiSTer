// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd_engine.h"

#include <memory>
#include <span>

#include "cores/cd_dialect_ports.h"
#include "cores/pce_scsi_engine.h"
#include "cores/saturn_cdd_engine.h"
#include "cores/sega_cdd_engine.h"
#include "cores/threedo_cdd_engine.h"
#include "infra/error.h"
#include "svc/disc_engine.h"

namespace mister::cores {

Ex<std::unique_ptr<CdEngine>> make_cd_engine(
    const CdProfile& profile,
    svc::DiscReadService& disc,
    const svc::DiscGeometry& geom,
    ICdTransport& transport,
    const os::IClock& clock,
    ICdFlowQuery* flow,
    ICdSectorEgress* egress,
    ICoreWindow* window
) {
    const CdDialectPorts ports{profile, disc, geom, transport, infra::opt_ref_of(egress)};
    switch (profile.dialect) {
        case CdDialect::SegaBcdNibble:
            return std::unique_ptr<CdEngine>(std::make_unique<SegaCddEngine>(ports, clock, flow));
        case CdDialect::PceScsi:
            return std::unique_ptr<CdEngine>(std::make_unique<PceScsiEngine>(ports, clock));
        case CdDialect::ThreeDo:
            return std::unique_ptr<CdEngine>(std::make_unique<ThreeDoCddEngine>(ports, clock));
        case CdDialect::SaturnRing:
            return std::unique_ptr<CdEngine>(
                std::make_unique<SaturnCddEngine>(ports, clock, window)
            );

        case CdDialect::SectorServed:
            break;
    }
    return std::unexpected(
        Error{Errc::core_load, ERR_SITE(), static_cast<std::uint32_t>(profile.dialect)}
    );
}

Ex<void> CdEngine::on_command_edge(std::uint8_t req) {

    if (req == last_req_) {
        command_wanted_ = false;
        return {};
    }
    last_req_ = req;
    command_wanted_ = true;
    return {};
}

Ex<void> CdEngine::accept_command(std::span<const std::uint16_t> words) {
    if (!command_wanted_) {

        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    command_wanted_ = false;
    if (words.size() != command_word_count()) {
        return std::unexpected(
            Error{Errc::negotiation, ERR_SITE(), static_cast<std::uint32_t>(words.size())}
        );
    }
    return do_accept(words);
}

Ex<void> CdEngine::on_tick() {

    const bool active = streaming();

    if (!cadence_.admit(active, do_speed_index()))
        return {};
    return do_tick();
}

Ex<void> CdEngine::push_sector() {

    return do_push_sector();
}

Ex<void> CdEngine::reset() {
    request_reset();
    return {};
}

bool CdEngine::apply_pending_reset() noexcept {

    if (!need_reset_)
        return false;
    need_reset_ = false;
    do_reset_machine();
    return true;
}

Ex<void> CdEngine::notify_disc(bool loaded) {
    return do_notify_disc(loaded);
}

}
