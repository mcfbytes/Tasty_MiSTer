// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_movie.h"

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include "cores/movie_codec.h"
#include "cores/movie_rom_match.h"
#include "cores/movie_system.h"
#include "svc/vfs.h"
#include "tasty_sentences.h"

namespace mister::fw {

tasty::Exit tasty_check_movie(const svc::Vfs& vfs, TastyFiles files) {
    if (!vfs.file_exists(files.movie)) {
        std::fprintf(
            stderr,
            "tasty: missing movie %.*s\n",
            static_cast<int>(files.movie.size()),
            files.movie.data()
        );
        return tasty::Exit::Code::Refused;
    }
    const auto sys = cores::movie_system_for(vfs, files.movie);
    if (!sys || !sys->codec) {
        const std::string why = cores::movie_unplayable_reason(vfs, files.movie);
        if (!why.empty()) {
            std::fprintf(stderr, "tasty: %s\n", why.c_str());
            return tasty::Exit::Code::Refused;
        }
        std::fprintf(
            stderr,
            "tasty: no codec for %.*s\n",
            static_cast<int>(files.movie.size()),
            files.movie.data()
        );
        return tasty::Exit::Code::Refused;
    }
    auto facts = cores::read_movie_facts(vfs, sys->codec->format(), files.movie);
    if (!facts) {
        const std::optional<cores::MovieRefusal> why = cores::movie_refusal_of(facts.error());
        const char* s = why ? tasty_movie_refusal_sentence(*why) : nullptr;
        if (s != nullptr) {
            std::fprintf(stderr, "tasty: %s\n", s);
        } else {
            std::fprintf(
                stderr,
                "tasty: cannot read movie %.*s\n",
                static_cast<int>(files.movie.size()),
                files.movie.data()
            );
        }
        return tasty::Exit::Code::Refused;
    }
    if (!facts->digest) {
        std::printf("no checksum in this movie\n");
        return tasty::Exit::Code::Ok;
    }
    if (!vfs.file_exists(files.rom)) {
        std::fprintf(
            stderr,
            "tasty: missing rom %.*s\n",
            static_cast<int>(files.rom.size()),
            files.rom.data()
        );
        return tasty::Exit::Code::Refused;
    }
    auto src = sys->codec->format().digest_source(vfs, files.rom);
    if (!src) {
        report_unhashable(*sys->codec, files.rom, src.error());
        return tasty::Exit::Code::Refused;
    }
    const cores::MovieRomMatch match{vfs, sys->codec->format()};
    const auto got = match.digest(*src, facts->digest->kind);
    if (!got.value) {
        if (got.fail == cores::MovieRomMatch::DigestFail::Open)
            std::fprintf(
                stderr,
                "tasty: cannot open rom %.*s\n",
                static_cast<int>(files.rom.size()),
                files.rom.data()
            );
        else
            std::fprintf(
                stderr,
                got.fail == cores::MovieRomMatch::DigestFail::Short ? "tasty: short rom read\n"
                                                                    : "tasty: rom read failed\n"
            );
        return tasty::Exit::Code::Refused;
    }
    if (match.matches(got, *facts)) {
        std::printf("rom matches\n");
        return tasty::Exit::Code::Ok;
    }
    std::printf("rom mismatch\n");
    return tasty::Exit::Code::Mismatch;
}

tasty::Exit tasty_info_movie(const svc::Vfs& vfs, std::string_view movie) {
    if (!vfs.file_exists(movie)) {
        std::fprintf(
            stderr,
            "tasty: missing movie %.*s\n",
            static_cast<int>(movie.size()),
            movie.data()
        );
        return tasty::Exit::Code::Refused;
    }
    const auto sys = cores::movie_system_for(vfs, movie);
    if (!sys || !sys->codec) {
        std::fprintf(
            stderr,
            "tasty: no codec for %.*s\n",
            static_cast<int>(movie.size()),
            movie.data()
        );
        return tasty::Exit::Code::Refused;
    }
    auto facts = cores::read_movie_facts(vfs, sys->codec->format(), movie);
    if (!facts) {
        std::fprintf(
            stderr,
            "tasty: cannot read movie %.*s\n",
            static_cast<int>(movie.size()),
            movie.data()
        );
        return tasty::Exit::Code::Refused;
    }
    std::printf(
        "system %.*s\nplays %s\n",
        static_cast<int>(sys->conf_str_name.size()),
        sys->conf_str_name.data(),
        cores::movie_system_supported(*sys) ? "yes" : "no"
    );
    const cores::MovieFrameTally
        count = cores::count_movie_frames(vfs, sys->codec->format(), movie, *facts);
    std::printf(count.capped ? "frames %s or more\n" : "frames %s\n", count.frames.text().c_str());
    if (facts->rerecords)
        std::printf("rerecords %s\n", facts->rerecords->text().c_str());
    else
        std::printf("rerecords unknown\n");
    return tasty::Exit::Code::Ok;
}

}
