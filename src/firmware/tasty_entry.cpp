// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_entry.h"

#include <cstddef>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cores/movie_codec.h"
#include "cores/movie_system.h"
#include "cores/registry.h"
#include "cores/rom_digest.h"
#include "hal/board_profile.h"
#include "hal/thread_map.h"
#include "infra/error.h"
#include "process_standup.h"
#include "replay_mode.h"
#include "svc/file.h"
#include "svc/vfs.h"
#include "tasty_cli.h"
#include "tasty_ctl.h"
#include "tasty_registry.h"

namespace {

int print_err(const mister::Error& e, const char* what) {
    std::fprintf(
        stderr,
        "tasty: %s failed: Errc=%u site=%u detail=%u\n",
        what,
        static_cast<unsigned>(e.code),
        static_cast<unsigned>(e.site),
        static_cast<unsigned>(e.detail)
    );
    return 1;
}

int cmd_info(const mister::fw::TastyArgs& a) {
    auto vfs = mister::svc::Vfs::create_at("/");
    if (!vfs)
        return print_err(vfs.error(), "Vfs");
    return mister::fw::tasty_info_movie(*vfs, a.movie.view());
}

int cmd_check(const mister::fw::TastyArgs& a) {
    auto vfs = mister::svc::Vfs::create_at("/");
    if (!vfs)
        return print_err(vfs.error(), "Vfs");
    return mister::fw::tasty_check_movie(*vfs, a.movie.view(), a.rom.view());
}

int cmd_rec_start_client(const mister::fw::TastyArgs& a) {
    if (!a.record) {
        std::fprintf(stderr, "tasty: rec start needs --record\n");
        return 1;
    }
    auto rec = mister::fw::tasty_prepare_record(*a.record, {});
    if (!rec)
        return 1;
    char buf[1400];
    const char* mode = a.hashes_only ? "hash" : "avi";
    const auto opt = mister::app::format_rec_options(a.rec);
    if (opt.empty())
        return 1;
    const int n = std::snprintf(
        buf,
        sizeof buf,
        "rec_start %.*s %s %.*s",
        static_cast<int>(rec->size()),
        rec->view().data(),
        mode,
        static_cast<int>(opt.size()),
        opt.view().data()
    );
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof buf)
        return 1;
    if (auto r = mister::fw::tasty_write_cmd(std::string_view(buf, static_cast<std::size_t>(n)));
        !r)
        return print_err(r.error(), "cmd");
    if (const auto blob = mister::hal::read_compatible(mister::hal::kCompatiblePath)) {
        if (const auto board = mister::hal::select_board(*blob))
            mister::fw::tasty_warn_record_writeback(
                rec->view(),
                mister::hal::seat_of((*board)->threads, mister::SeatTag::RT).cpu
            );
    }
    return 0;
}

int cmd_client(mister::fw::TastyVerb v) {
    return mister::fw::tasty_run_client(v);
}

int run_owner(const mister::fw::TastyArgs& a, std::optional<mister::fw::ArgvArea> area) {
    mister::fw::ReplayMode mode{a, area};
    mister::fw::ProcessStandup standup{mode};
    return standup.run();
}

}

std::optional<std::span<const char* const>> mister::fw::tasty_invocation(
    std::span<const char* const> argv
) noexcept {
    if (argv.empty() || argv[0] == nullptr)
        return std::nullopt;
    const std::string_view prog{argv[0]};
    const std::size_t slash = prog.rfind('/');
    const std::string_view base = slash == std::string_view::npos ? prog : prog.substr(slash + 1);
    if (base.find("tasty") != std::string_view::npos)
        return argv;
    if (argv.size() >= 2 && argv[1] != nullptr && std::string_view{argv[1]} == "tasty")
        return argv.subspan(1);
    return std::nullopt;
}

int mister::fw::run_tasty(std::span<const char* const> argv, std::optional<ArgvArea> area) {
    mister::fw::tasty_adopt_owner_comm();
    if (argv.size() < 2) {
        mister::fw::tasty_print_usage();
        return 1;
    }
    auto parsed = mister::fw::parse_tasty_args(argv);
    if (!parsed) {
        std::fprintf(stderr, "tasty: %s\n", mister::fw::tasty_args_refusal(parsed.error().detail));
        return 1;
    }
    if (!mister::fw::tasty_resolve_args(*parsed)) {
        std::fprintf(stderr, "tasty: cannot resolve paths\n");
        return 1;
    }
    const mister::fw::TastyArgs& a = *parsed;
    switch (a.verb) {
        case mister::fw::TastyVerb::Help:
            mister::fw::tasty_print_usage(stdout);
            return 0;
        case mister::fw::TastyVerb::Info:
            return cmd_info(a);
        case mister::fw::TastyVerb::Check:
            return cmd_check(a);
        case mister::fw::TastyVerb::Status:
        case mister::fw::TastyVerb::Stop:
        case mister::fw::TastyVerb::RecStop:
            return cmd_client(a.verb);
        case mister::fw::TastyVerb::Play:
            return run_owner(a, area);
        case mister::fw::TastyVerb::RecStart:
            if (mister::fw::tasty_rec_start_joins_owner())
                return cmd_rec_start_client(a);
            return run_owner(a, area);
    }
    return 1;
}
