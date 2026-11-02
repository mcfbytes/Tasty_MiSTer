// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd_engine.h"

#include <memory>
#include <span>

#include "cores/cd_dialect_facts.h"
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
    const os::IClock& clock,
    ICoreWindow* window
) {
    const CdDialectFacts facts{profile, disc};
    switch (profile.dialect) {
        case CdDialect::SegaBcdNibble:
            return std::unique_ptr<CdEngine>(std::make_unique<SegaCddEngine>(facts, clock));
        case CdDialect::PceScsi:
            return std::unique_ptr<CdEngine>(std::make_unique<PceScsiEngine>(facts, clock));
        case CdDialect::ThreeDo:
            return std::unique_ptr<CdEngine>(std::make_unique<ThreeDoCddEngine>(facts, clock));
        case CdDialect::SaturnRing:
            return std::unique_ptr<CdEngine>(
                std::make_unique<SaturnCddEngine>(facts, clock, window)
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

Ex<void> CdEngine::accept_command(std::span<const std::uint16_t> words, CdDialectPorts& p) {
    if (!command_wanted_) {

        return std::unexpected(Error{Errc::negotiation, ERR_SITE(), 0});
    }
    command_wanted_ = false;
    if (words.size() != command_word_count()) {
        return std::unexpected(
            Error{Errc::negotiation, ERR_SITE(), static_cast<std::uint32_t>(words.size())}
        );
    }
    return do_accept(words, p);
}

Ex<void> CdEngine::on_tick(CdDialectPorts& p) {

    const bool active = streaming(p.geom);

    if (!cadence_.admit(active, do_speed_index()))
        return {};
    return do_tick(p);
}

Ex<void> CdEngine::push_sector(CdDialectPorts& p) {

    return do_push_sector(p);
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

Ex<void> CdEngine::notify_disc(bool loaded, const svc::DiscGeometry& geom) {
    return do_notify_disc(loaded, geom);
}

}
