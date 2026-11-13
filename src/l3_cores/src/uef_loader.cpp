// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/uef_loader.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "cores/uef.h"
#include "infra/ascii.h"
#include "infra/path_parts.h"

namespace mister::cores {
namespace {

[[nodiscard]] Ex<std::vector<std::uint8_t>> read_whole(svc::IFile& f, proto::FileSize size) {
    std::vector<std::uint8_t> bytes(size.file_word<std::size_t>());
    if (bytes.empty())
        return bytes;
    if (auto r = svc::read_exact(f, proto::FileSize{}, std::as_writable_bytes(std::span(bytes)));
        !r) {
        return std::unexpected(r.error());
    }
    return bytes;
}

}

Ex<LoadPlan> UefLoader::plan(const LoadAsk&, const proto::StatusWord&) {
    TASTY_SEAT_BODY(UefLoader);
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
}

Ex<std::unique_ptr<svc::IFile>> UefLoader::source(
    const LoadAsk& ask,
    std::unique_ptr<svc::IFile> raw
) {
    TASTY_SEAT_BODY(UefLoader);
    if (ask.size.empty() || raw == nullptr)
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});

    if (!infra::ieq(infra::extension(ask.path), ".UEF"))
        return raw;

    if (ask.size < uef::kMinImageBytes)
        return uef::make_tape_file({});

    std::array<std::uint8_t, 2> magic{};
    if (auto r = svc::read_exact(*raw, proto::FileSize{}, std::as_writable_bytes(std::span(magic)));
        !r) {
        return std::unexpected(r.error());
    }

    std::vector<std::uint8_t> image;
    if (svc::GzipInflater::is_gzip(std::span<const std::uint8_t>(magic))) {
        auto whole = read_whole(*raw, ask.size);
        if (!whole)
            return std::unexpected(whole.error());
        auto inflated = inflater_.inflate(*whole, uef::kMaxImageBytes);
        if (!inflated)
            return std::unexpected(inflated.error());
        image = std::move(*inflated);
    } else if (ask.size > uef::kMaxImageBytes) {
        return uef::make_tape_file({});
    } else {
        auto whole = read_whole(*raw, ask.size);
        if (!whole)
            return std::unexpected(whole.error());
        image = std::move(*whole);
    }
    return uef::make_tape_file(std::move(image));
}

}
