// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cd_engine.h"

#include <memory>
#include <optional>
#include <span>

#include "cores/cd_dialect_facts.h"
#include "cores/cd_dialect_ports.h"
#include "cores/pce_scsi_engine.h"
#include "cores/saturn_cdd_engine.h"
#include "cores/sega_cdd_engine.h"
#include "cores/threedo_cdd_engine.h"
#include "infra/error.h"
#include "svc/disc_geometry.h"

namespace mister::cores {

namespace {

SectorIo sector_io(const CdProfile& profile) {
    const std::optional<SectorIo> io = profile.io.sectors();
    if (!io)
        fatal(Error{Errc::core_load, ERR_SITE(), 0}, "make_cd_engine");
    return *io;
}

DescriptorIo descriptor_io(const CdProfile& profile) {
    const std::optional<DescriptorIo> io = profile.io.descriptor();
    if (!io)
        fatal(Error{Errc::core_load, ERR_SITE(), 0}, "make_cd_engine");
    return *io;
}

}

std::unique_ptr<CdEngine> make_cd_engine(
    const CdProfile& profile,
    svc::DiscReadService& disc,
    const os::IClock& clock,
    infra::OptRef<ICoreWindow> window
) {
    const CdDialectFacts facts{profile, disc};

    switch (profile.row.index()) {
        case 0:
            return std::make_unique<SegaCddEngine>(
                facts,
                std::get<CdProfile::SegaCddFacts>(profile.row),
                clock,
                sector_io(profile)
            );
        case 1:
            return std::make_unique<PceScsiEngine>(
                facts,
                std::get<CdProfile::PceScsiFacts>(profile.row),
                clock,
                sector_io(profile)
            );
        default:
            break;
    }

    if (const auto* row = std::get_if<CdProfile::SaturnFacts>(&profile.row)) {
        return std::make_unique<SaturnCddEngine>(
            facts,
            *row,
            clock,
            window,
            descriptor_io(profile)
        );
    }
    if (const auto* row = std::get_if<CdProfile::ThreeDoFacts>(&profile.row)) {
        return std::make_unique<ThreeDoCddEngine>(facts, *row, clock);
    }

    fatal(
        Error{Errc::core_load, ERR_SITE(), static_cast<std::uint32_t>(profile.row.index())},
        "make_cd_engine"
    );
}

bool CdEngine::on_command_edge(std::uint8_t req) noexcept {

    if (req == last_req_) {
        command_wanted_ = false;
        return false;
    }
    last_req_ = req;
    command_wanted_ = true;
    return true;
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

void CdEngine::reset() noexcept {
    request_reset();
}

bool CdEngine::apply_pending_reset() noexcept {

    if (!need_reset_)
        return false;
    need_reset_ = false;
    do_reset_machine();
    return true;
}

void CdEngine::notify_disc(bool loaded, const svc::DiscGeometry& geom) noexcept {
    do_notify_disc(loaded, geom);
}

}
