// SPDX-License-Identifier: GPL-3.0-or-later
#include "tasty_record.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <string>

#include "infra/ascii.h"
#include "infra/error.h"
#include "tasty_cli_refusal.h"

namespace mister::fw {

namespace {

[[nodiscard]] Ex<app::PathText> resolved_path(app::PathText p) noexcept {
    if (p.empty())
        return p;
    if (p.view()[0] != '/') {
        char cwd[PATH_MAX];
        if (::getcwd(cwd, sizeof cwd) == nullptr)
            return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
        const std::string absolute = std::string(cwd) + "/" + std::string(p.view());
        if (!p.assign(absolute))
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    char real[PATH_MAX];
    if (::realpath(p.c_str(), real) != nullptr && !p.assign(real))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    return p;
}

std::string_view stem_of(std::string_view path) noexcept {
    const std::size_t slash = path.rfind('/');
    std::string_view base = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const std::size_t dot = base.rfind('.');
    if (dot != std::string_view::npos && dot > 0)
        base = base.substr(0, dot);
    return base;
}

bool is_dir(const char* p) noexcept {
    struct stat st{};
    return ::stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

bool is_file(const char* p) noexcept {
    struct stat st{};
    return ::stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

constexpr std::string_view kAviSuffix = ".avi";

bool ends_with_avi(std::string_view s) noexcept {
    return infra::iends_with(s, kAviSuffix);
}

[[nodiscard]] Ex<void> mkdir_p(std::string_view path) noexcept {
    if (path.empty())
        return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), EINVAL));
    std::string s(path);
    while (!s.empty() && s.back() == '/')
        s.pop_back();
    if (s.empty())
        return {};
    std::string cur;
    std::size_t i = 0;
    if (s[0] == '/') {
        cur = "/";
        i = 1;
    }
    while (i < s.size()) {
        const auto slash = s.find('/', i);
        const auto part = s.substr(i, slash == std::string::npos ? std::string::npos : slash - i);
        if (!part.empty()) {
            if (cur.size() > 1 || (!cur.empty() && cur[0] != '/'))
                cur += '/';
            cur += part;
            if (::mkdir(cur.c_str(), S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0
                && errno != EEXIST)
                return std::unexpected(infra::errno_error(Errc::os, ERR_SITE(), errno));
        }
        if (slash == std::string::npos)
            break;
        i = slash + 1;
    }
    return {};
}

}

[[nodiscard]] Ex<TastyArgs> tasty_resolve_args(TastyArgs args) noexcept {
    auto movie = resolved_path(args.movie);
    if (!movie)
        return std::unexpected(movie.error());
    auto rom = resolved_path(args.rom);
    if (!rom)
        return std::unexpected(rom.error());
    auto core = resolved_path(args.core);
    if (!core)
        return std::unexpected(core.error());
    args.movie = *movie;
    args.rom = *rom;
    args.core = *core;
    if (!args.record)
        return args;
    auto record = resolved_path(*args.record);
    if (!record)
        return std::unexpected(record.error());
    args.record = *record;
    return args;
}

std::string_view tasty_record_dir(std::string_view record) noexcept {
    if (!record.empty() && record.back() == '/') {
        record.remove_suffix(1);
    } else {
        const auto slash = record.rfind('/');
        if (slash == std::string_view::npos)
            return ".";
        record = record.substr(0, slash);
    }
    return record.empty() ? std::string_view("/") : record;
}

[[nodiscard]] Ex<app::PathText> tasty_prepare_record(
    app::PathText record,
    std::string_view movie
) noexcept {
    std::string path(record.view());
    const bool trailing = !path.empty() && path.back() == '/';
    if (trailing)
        path.pop_back();

    auto refuse = [](TastyRefusal why, const std::string& p) -> Ex<app::PathText> {
        std::fprintf(stderr, "tasty: %s (%s)\n", why.sentence(), p.c_str());
        return std::unexpected(why.error(ERR_SITE()));
    };
    if (path.empty())
        return refuse(TastyRefusal::RecordNoTarget, std::string(record.view()));

    if (!trailing && ends_with_avi(path)) {
        if (is_dir(path.c_str()))
            return refuse(TastyRefusal::RecordIsDirectory, path);
        const auto slash = path.rfind('/');
        const std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
        if (base.size() <= kAviSuffix.size())
            return refuse(TastyRefusal::RecordNoStem, path);
        path.replace(path.size() - kAviSuffix.size(), kAviSuffix.size(), kAviSuffix);
        if (slash != std::string::npos && slash > 0) {
            if (auto made = mkdir_p(path.substr(0, slash)); !made)
                return refuse(TastyRefusal::RecordParentFailed, path);
        }
        if (!record.assign(path))
            return refuse(TastyRefusal::RecordTooLong, path);
        return record;
    }
    if (is_file(path.c_str()))
        return refuse(TastyRefusal::RecordIsFile, path);
    if (auto made = mkdir_p(path); !made)
        return refuse(TastyRefusal::RecordDirFailed, path);
    if (movie.empty()) {
        path += '/';
        if (!record.assign(path))
            return refuse(TastyRefusal::RecordTooLong, path);
        return record;
    }
    const std::string joined = path + "/" + std::string(stem_of(movie));
    if (!record.assign(joined))
        return refuse(TastyRefusal::RecordTooLong, path);
    return record;
}

}
