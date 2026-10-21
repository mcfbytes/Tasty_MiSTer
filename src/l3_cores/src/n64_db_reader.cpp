// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_db_reader.h"

#include <cstring>
#include <span>

#include "cores/manifests/n64.h"
#include "cores/n64_db.h"

namespace mister::cores {

namespace {

[[nodiscard]] bool line_end(char c) noexcept {
    return c == '\0' || c == '\r' || c == '\n';
}

}

n64::DbTags N64DbReader::scan(n64::DbKey kind, std::string_view key) {
    TASTY_SEAT_BODY(N64DbReader);
    for (std::string_view name : n64::kDbFileNames) {
        const n64::DbTags t = scan_file_(name, kind, key);
        if (t.found)
            return t;
    }
    return {};
}

n64::DbTags N64DbReader::scan_file_(std::string_view name, n64::DbKey kind, std::string_view key) {
    const std::string_view dir = core_dir_;
    if (dir.size() + 1 + name.size() > kRelChars)
        return {};
    char rel[kRelChars];
    std::memcpy(rel, dir.data(), dir.size());
    rel[dir.size()] = '/';
    std::memcpy(rel + dir.size() + 1, name.data(), name.size());
    const std::string_view rel_view(rel, dir.size() + 1 + name.size());
    const svc::SearchPolicy policy{manifests::kN64HomeDir, true};
    auto path = vfs_.resolve(rel_view, policy);
    if (!path)
        return {};
    auto file = vfs_.open(*path, svc::OpenMode::ReadWhole);
    if (!file)
        return {};

    std::uint64_t off = 0;
    std::size_t carry = 0;
    bool skipping = false;
    for (;;) {
        auto got = (*file)->read_at(
            off,
            std::as_writable_bytes(std::span<char>(buf_ + carry, kChunkBytes - carry))
        );
        if (!got)
            return {};
        const bool eof = (*got == 0);
        std::size_t filled = carry + *got;
        off += *got;
        if (filled == 0)
            return {};

        if (skipping) {
            std::size_t i = 0;
            while (i < filled && !line_end(buf_[i]))
                ++i;
            if (i == filled) {
                carry = 0;
                if (eof)
                    return {};
                continue;
            }
            skipping = false;
            std::memmove(buf_, buf_ + i, filled - i);
            filled -= i;
        }

        std::size_t cut = filled;
        if (!eof) {
            while (cut > 0 && !line_end(buf_[cut - 1]))
                --cut;
            if (cut == 0 && filled < kChunkBytes) {

                carry = filled;
                continue;
            }
            if (cut == 0) {

                ++long_lines_;
                skipping = true;
                carry = 0;
                continue;
            }
        }

        const n64::DbTags t = n64::lookup(std::string_view(buf_, cut), kind, key);
        if (t.found)
            return t;
        if (eof)
            return {};
        carry = filled - cut;
        std::memmove(buf_, buf_ + cut, carry);
    }
}

}
