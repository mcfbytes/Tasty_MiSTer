// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstddef>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cores/movie_codec.h"
#include "cores/movie_system.h"
#include "cores/registry.h"
#include "cores/rom_digest.h"
#include "infra/error.h"
#include "svc/file.h"
#include "svc/vfs.h"
#include "tasty_cli.h"
#include "tasty_ctl.h"
#include "tasty_hub.h"
#include "tasty_registry.h"

namespace {

int print_err(const mister::Error& e, const char* what) {
    std::fprintf(stderr, "tasty: %s failed: Errc=%u site=%u detail=%u\n", what,
                 static_cast<unsigned>(e.code), static_cast<unsigned>(e.site),
                 static_cast<unsigned>(e.detail));
    return 1;
}

int cmd_info(const mister::fw::TastyArgs& a) {
    auto vfs = mister::svc::Vfs::create_at("/");
    if (!vfs) return print_err(vfs.error(), "Vfs");
    return mister::fw::tasty_info_movie(*vfs, a.movie.view());
}

int cmd_check(const mister::fw::TastyArgs& a) {
    auto vfs = mister::svc::Vfs::create_at("/");
    if (!vfs) return print_err(vfs.error(), "Vfs");
    return mister::fw::tasty_check_movie(*vfs, a.movie.view(), a.rom.view());
}

int cmd_rec_start_client(const mister::fw::TastyArgs& a) {
    if (a.record.empty()) {
        std::fprintf(stderr, "tasty: rec start needs --record\n");
        return 1;
    }
    auto rec = mister::fw::tasty_prepare_record(a.record, {});
    if (!rec) {
        std::fprintf(stderr, "tasty: cannot create record path %s\n", a.record.c_str());
        return 1;
    }
    char buf[1100];
    const char* mode = a.hashes_only ? "hash" : "avi";
    const int n = std::snprintf(buf, sizeof buf, "rec_start %.*s %s", static_cast<int>(rec->size()),
                                rec->view().data(), mode);
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof buf) return 1;
    if (auto r = mister::fw::tasty_write_cmd(std::string_view(buf, static_cast<std::size_t>(n)));
        !r)
        return print_err(r.error(), "cmd");
    return 0;
}

int cmd_client(mister::fw::TastyVerb v) { return mister::fw::tasty_run_client(v); }

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        mister::fw::tasty_print_usage();
        return 1;
    }
    auto parsed = mister::fw::parse_tasty_args(std::span<const char* const>(
        const_cast<const char**>(argv), static_cast<std::size_t>(argc)));
    if (!parsed) return print_err(parsed.error(), "args");
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
            return mister::fw::tasty_run_owner(a);
        case mister::fw::TastyVerb::RecStart:
            if (mister::fw::tasty_rec_start_joins_owner()) return cmd_rec_start_client(a);
            return mister::fw::tasty_run_owner(a);
    }
    return 1;
}
