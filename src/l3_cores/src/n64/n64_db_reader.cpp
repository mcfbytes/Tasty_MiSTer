// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64/n64_db_reader.h"

#include <cstring>
#include <span>

#include "cores/boot_asset.h"
#include "cores/n64/n64_db.h"
#include "cores/n64/n64_path.h"

namespace mister::cores::n64 {

namespace {

[[nodiscard]] bool line_end(char c) noexcept {
    return c == '\0' || c == '\r' || c == '\n';
}

}

std::optional<n64::DbMatch> N64DbReader::scan(const n64::DbKey& key) {
    TASTY_SEAT_BODY(N64DbReader);
    for (std::string_view name : n64::kDbFileNames)
        if (auto match = scan_file_(name, key))
            return match;
    return std::nullopt;
}

std::optional<n64::DbMatch> N64DbReader::scan_file_(std::string_view name, const n64::DbKey& key) {
    const auto rel = n64::core_relative(core_dir_, name);
    if (!rel)
        return std::nullopt;
    const svc::SearchPolicy policy{kCoreHomeDir, true};
    auto path = vfs_.resolve(rel->view(), policy);
    if (!path)
        return std::nullopt;
    auto file = vfs_.open(*path, svc::OpenMode::ReadWhole);
    if (!file)
        return std::nullopt;

    proto::FileOffset off = proto::kFileStart;
    std::size_t carry = 0;
    bool skipping = false;
    for (;;) {
        const std::span<std::byte> room = std::as_writable_bytes(
            std::span<char>(buf_ + carry, kChunkBytes - carry)
        );
        auto got = (*file)->read_at(off, room);
        if (!got)
            return std::nullopt;
        const bool eof = got->empty();
        std::size_t filled = carry + got->first_in(room).size();
        off += proto::FileSize::of(*got);
        if (filled == 0)
            return std::nullopt;

        if (skipping) {
            std::size_t i = 0;
            while (i < filled && !line_end(buf_[i]))
                ++i;
            if (i == filled) {
                carry = 0;
                if (eof)
                    return std::nullopt;
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

        if (auto match = n64::lookup(std::string_view(buf_, cut), key))
            return match;
        if (eof)
            return std::nullopt;
        carry = filled - cut;
        std::memmove(buf_, buf_ + cut, carry);
    }
}

}
