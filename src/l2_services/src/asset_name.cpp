// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/asset_name.h"

#include <cstddef>
#include <string_view>

#include "infra/ascii.h"
#include "vfs_detail.h"

namespace mister::svc::asset_name {

bool extension_matches(std::string_view filename, std::string_view packed_extensions) {
    constexpr std::size_t kExtFieldChars = 3;
    constexpr std::size_t kExtBufChars = kExtFieldChars + 1;
    constexpr std::size_t kExtSecondChar = 1;
    constexpr std::size_t kExtThirdChar = 2;

    const std::size_t dot = filename.rfind('.');
    if (dot == std::string_view::npos)
        return false;
    const std::string_view fext = filename.substr(dot + 1);

    std::size_t pos = 0;
    while (pos < packed_extensions.size()) {

        char e[kExtBufChars] = {};
        const std::size_t avail = packed_extensions.size() - pos;
        const std::size_t n = (avail < kExtFieldChars) ? avail : kExtFieldChars;
        for (std::size_t k = 0; k < n; ++k)
            e[k] = packed_extensions[pos + k];
        if (e[kExtThirdChar] == ' ') {
            e[kExtThirdChar] = '\0';
            if (e[kExtSecondChar] == ' ')
                e[kExtSecondChar] = '\0';
        }

        bool found = true;
        for (std::size_t i = 0; i < kExtBufChars; ++i) {
            if (e[i] == '*')
                break;
            const char c = (i < fext.size()) ? fext[i] : '\0';
            if (e[i] == '?' && c != '\0')
                continue;
            if (infra::ascii_lower(e[i]) != infra::ascii_lower(c))
                found = false;

            if (e[i] == '\0' || !found)
                break;
        }
        if (found)
            return true;

        if (avail < kExtFieldChars)
            break;
        pos += kExtFieldChars;
    }
    return false;
}

std::optional<Crc32> parse_crc_name(std::string_view filename, std::string_view ext) {
    constexpr std::size_t kCrcHexDigits = 8;
    constexpr std::size_t kCrcTagChars = kCrcHexDigits + 2;

    const std::size_t len = filename.size();
    const std::size_t extlen = ext.size();
    if (len < kCrcTagChars + extlen)
        return std::nullopt;
    if (!infra::iends_with(filename, ext))
        return std::nullopt;

    const std::size_t bracket = len - kCrcTagChars - extlen;
    if (filename[bracket] != '[')
        return std::nullopt;
    if (filename[len - extlen - 1] != ']')
        return std::nullopt;

    const auto v = detail::scan_hex8(filename.substr(bracket + 1, kCrcHexDigits));
    if (!v)
        return std::nullopt;
    return Crc32{*v};
}

ZipSplit zip_split(std::string_view path) noexcept {
    constexpr std::size_t kZipSuffixChars = 4;
    const std::optional<infra::TextOffset> z = infra::ifind(path, ".zip");
    if (!z)
        return {};
    const infra::TextOffset end = z->advanced(infra::ByteCount{kZipSuffixChars});
    const infra::TextOffset member_at = end.advanced(infra::ByteCount{1});
    ZipSplit s;
    s.zipped = true;
    s.archive = end.prefix_of(path);
    s.member = member_at.inside(path) ? member_at.suffix_after(path) : std::string_view{};
    return s;
}

}
