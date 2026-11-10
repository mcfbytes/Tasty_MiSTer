// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "cores/digest_value.h"
#include "cores/movie_facts.h"
#include "cores/movie_frame.h"
#include "cores/types.h"
#include "infra/error.h"
#include "infra/opt_ref.h"
#include "proto/types.h"
#include "svc/file.h"

namespace mister::svc {
class Vfs;
}

namespace mister::cores {

struct MovieArchiveFormat;

class IMovieFormat {
public:
    struct DigestSpan {
        proto::FileSize offset{};
        proto::FileSize length{};
    };

    static constexpr std::size_t kRomHead = 16;

    static constexpr std::uint64_t kRomMax = 4u << 20;

    struct KeyValue {
        std::string_view key;
        std::string_view value;
    };

    struct DigestSource {
        std::string file{};
        DigestSpan span{};
        std::vector<std::uint8_t> prefix{};

        std::vector<std::vector<std::uint8_t>> alt_prefixes{};
    };

    virtual ~IMovieFormat() = default;

    [[nodiscard]] static KeyValue split(std::string_view line) noexcept;

    [[nodiscard]] static std::string_view trim(std::string_view s) noexcept;

    [[nodiscard]] virtual infra::OptRef<const MovieArchiveFormat> archive() const noexcept {
        return {};
    }

    [[nodiscard]] virtual Ex<std::unique_ptr<svc::IFile>> open_movie(
        const svc::Vfs& vfs,
        std::string_view path
    ) const {
        return open_plain_or_archive(archive(), vfs, path);
    }

    [[nodiscard]] virtual bool starts_log(std::string_view line) const noexcept = 0;

    [[nodiscard]] virtual bool ends_log(std::string_view line) const noexcept {
        (void)line;
        return false;
    }

    [[nodiscard]] virtual Ex<MovieFacts> header_line(
        std::string_view line,
        const MovieFacts& so_far
    ) const noexcept = 0;

    [[nodiscard]] virtual Ex<void> finish_header(const MovieFacts& f) const noexcept = 0;
    [[nodiscard]] virtual Ex<MovieFrame> frame(
        std::string_view line,
        const MovieFacts& f
    ) const noexcept = 0;

    [[nodiscard]] virtual ReplayLead default_lead(const MovieFacts& f) const noexcept = 0;

    [[nodiscard]] virtual std::optional<DigestSpan> rom_digest_span(
        std::span<const std::uint8_t> head,
        proto::FileSize size
    ) const noexcept {
        (void)head;
        (void)size;
        return std::nullopt;
    }

    [[nodiscard]] virtual bool rom_matches(
        const DigestValue& d,
        const MovieFacts& f
    ) const noexcept {
        return f.digest && d == *f.digest;
    }

    [[nodiscard]] virtual Ex<DigestSource> digest_source(
        const svc::Vfs& vfs,
        std::string_view rom
    ) const {
        return head_digest_source(vfs, rom);
    }

protected:
    [[nodiscard]] Ex<DigestSource> head_digest_source(
        const svc::Vfs& vfs,
        std::string_view rom
    ) const;

    [[nodiscard]] static Ex<std::unique_ptr<svc::IFile>> open_plain_or_archive(
        infra::OptRef<const MovieArchiveFormat> container,
        const svc::Vfs& vfs,
        std::string_view path
    );

    IMovieFormat() = default;
    IMovieFormat(const IMovieFormat&) = default;
    IMovieFormat& operator=(const IMovieFormat&) = default;
};

}
