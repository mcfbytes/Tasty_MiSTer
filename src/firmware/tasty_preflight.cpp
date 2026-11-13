// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_preflight.h"

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include "app/replay_feeder.h"
#include "cores/movie_codec.h"
#include "cores/movie_rom_match.h"
#include "cores/movie_system.h"
#include "svc/vfs.h"
#include "tasty_cli_refusal.h"
#include "tasty_say.h"
#include "tasty_sentences.h"

namespace mister::fw {

namespace {

[[nodiscard]] Ex<app::PathText> rom_search(
    const svc::Vfs& vfs,
    const cores::IMovieFormat& format,
    const cores::MovieFacts& facts,
    std::string_view folder
) {
    using Search = cores::MovieRomMatch::Search;
    const cores::MovieRomMatch match{vfs, format};
    const Search found = match.find_rom(facts, folder);
    const std::string at = "/" + std::string(cores::MovieRomMatch::kGamesRel) + std::string(folder);
    switch (found.end) {
        case Search::End::Found: {
            app::PathText rom{};
            if (!rom.assign(found.rom)) {
                TastyRefusal{TastyRefusal::RomPathTooLong}.say();
                return std::unexpected(
                    TastyRefusal{TastyRefusal::RomPathTooLong}.error(ERR_SITE())
                );
            }
            return rom;
        }
        case Search::End::NoChecksum:
            TastyRefusal{TastyRefusal::RomNeedsChecksum}.say();
            return std::unexpected(TastyRefusal{TastyRefusal::RomNeedsChecksum}.error(ERR_SITE()));
        case Search::End::Stopped:
            std::fprintf(
                stderr,
                "tasty: no ROM among the first %s files in %s matches this "
                "movie (the search stops there); pass --rom\n",
                found.names.text().c_str(),
                at.c_str()
            );
            return std::unexpected(TastyRefusal{TastyRefusal::RomSearchStopped}.error(ERR_SITE()));
        case Search::End::NoMatch:
            break;
    }
    std::fprintf(stderr, "tasty: no ROM in %s matches this movie; pass --rom\n", at.c_str());
    return std::unexpected(TastyRefusal{TastyRefusal::RomNotFound}.error(ERR_SITE()));
}

}

Ex<app::PathText> TastyPreflight::play_rom(const TastyArgs& a) const {

    if (a.loop && a.record && !a.record->empty()) {
        TastyRefusal{TastyRefusal::LoopWithRecord}.say();
        return std::unexpected(TastyRefusal{TastyRefusal::LoopWithRecord}.error(ERR_SITE()));
    }
    const auto sys = cores::movie_system_for(vfs_, a.movie.view());
    if (!sys || !sys->codec) {
        std::fprintf(stderr, "tasty: no codec for %s\n", a.movie.c_str());
        return std::unexpected(TastyRefusal{TastyRefusal::NoCodec}.error(ERR_SITE()));
    }
    if (a.ram_fill && !sys->codec->core().ram_fill_need(*a.ram_fill)) {
        TastyRefusal{TastyRefusal::RamInitUnsupported}.say();
        return std::unexpected(TastyRefusal{TastyRefusal::RamInitUnsupported}.error(ERR_SITE()));
    }
    auto facts = cores::read_movie_facts(vfs_, sys->codec->format(), a.movie.view());
    if (!facts) {
        std::fprintf(stderr, "tasty: cannot read movie %s\n", a.movie.c_str());
        return std::unexpected(TastyRefusal{TastyRefusal::MovieUnreadable}.error(ERR_SITE()));
    }
    const cores::ReplayLead lead = a.lead ? a.lead->as_lead()
                                          : sys->codec->format().default_lead(*facts);
    const auto range = sys->codec->core().lead_range();
    if (!range.admits(lead) || !app::ArmLead::of(lead)) {
        std::fprintf(
            stderr,
            "tasty: --lead %s is outside %s for this movie; "
            "pass a lead inside that range\n",
            lead.text().c_str(),
            range.text().c_str()
        );
        return std::unexpected(TastyRefusal{TastyRefusal::LeadOutOfRange}.error(ERR_SITE()));
    }
    if (a.phase_us) {
        if (!app::ReplayFeeder::phase_fits(
                *a.phase_us,
                sys->codec->core().raster(*facts).period_ns
            )) {
            TastyRefusal{TastyRefusal::PhaseOutOfRange}.say();
            return std::unexpected(TastyRefusal{TastyRefusal::PhaseOutOfRange}.error(ERR_SITE()));
        }
    }
    if (a.rom.empty()) {
        const std::string_view named = sys->codec->core().games_folder();
        const std::string_view folder = named.empty() ? sys->conf_str_name : named;
        return rom_search(vfs_, sys->codec->format(), *facts, folder);
    }
    if (auto ok = rom_readable(TastyFiles{.movie = a.movie.view(), .rom = a.rom.view()}); !ok)
        return std::unexpected(ok.error());
    return a.rom;
}

Ex<void> TastyPreflight::rom_readable(TastyFiles files) const {
    const std::string why = cores::movie_unplayable_reason(vfs_, files.movie);
    if (!why.empty()) {
        std::fprintf(stderr, "tasty: %s\n", why.c_str());
        return std::unexpected(TastyRefusal{TastyRefusal::RomUnplayable}.error(ERR_SITE()));
    }
    const auto sys = cores::movie_system_for(vfs_, files.movie);
    if (!sys || !sys->codec)
        return {};
    auto src = sys->codec->format().digest_source(vfs_, files.rom);
    if (src)
        return {};
    report_unhashable(*sys->codec, files.rom, src.error());
    return std::unexpected(TastyRefusal{TastyRefusal::RomUnhashable}.error(ERR_SITE()));
}

}
