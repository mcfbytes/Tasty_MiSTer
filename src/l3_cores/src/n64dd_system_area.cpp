// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64dd_system_area.h"

#include <algorithm>
#include <cstring>
#include <span>

#include "infra/byte_view.h"

namespace mister::cores::n64dd {
namespace {

using Block = std::array<std::uint8_t, kZone0BlockBytes>;

[[nodiscard]] bool read_exact(svc::IFile& f, std::uint64_t off, std::span<std::uint8_t> dst) {
    std::size_t got = 0;
    while (got < dst.size()) {
        auto n = f.read_at(off + got, std::as_writable_bytes(dst.subspan(got)));
        if (!n || *n == 0)
            return false;
        got += *n;
    }
    return true;
}

[[nodiscard]] bool repeats(const Block& b, std::uint32_t repeat, std::uint32_t size) {
    if (b.size() < std::size_t{repeat} * size)
        return false;
    for (std::uint32_t i = 0; i < size; ++i) {
        for (std::uint32_t j = 1; j < repeat; ++j) {
            if (b[i] != b[j * size + i])
                return false;
        }
    }
    return true;
}

[[nodiscard]] bool valid_system_block(const Block& b, std::uint32_t sector, bool dev) {
    const bool retail = (b[0] == 0xE8 && b[1] == 0x48 && b[2] == 0xD3 && b[3] == 0x16)
        || (b[0] == 0x22 && b[1] == 0x63 && b[2] == 0xEE && b[3] == 0x56);
    const bool devm = b[0] == 0x00 && b[1] == 0x00 && b[2] == 0x00 && b[3] == 0x00;
    if (dev ? !devm : !retail)
        return false;
    if (b[4] != 0x10)
        return false;
    if (b[5] < 0x10 || b[5] >= 0x17)
        return false;
    if (b[0x18] != 0xFF || b[0x19] != 0xFF || b[0x1A] != 0xFF || b[0x1B] != 0xFF)
        return false;
    if (b[0x1C] != 0x80)
        return false;
    return repeats(b, kSectorsPerBlock, sector);
}

[[nodiscard]] Ex<bool> read_disk_id(svc::IFile& f, Block& b, SystemArea& out) {
    bool valid = false;
    for (const std::uint32_t lba : {15u, 14u}) {
        if (!read_exact(f, std::uint64_t{lba} * kZone0BlockBytes, b))
            continue;
        if (!repeats(b, kSectorsPerBlock, kSystemSectorBytes))
            continue;
        if (out.disk_id.empty() || out.disk_id.view()[0] == '\0') {
            out.disk_id.clear();
            (void)out.disk_id.assign(infra::as_text(std::span(b).first(4)));
        }
        valid = true;
    }
    return valid;
}

struct Pass {
    std::array<std::uint8_t, 4> blocks;
    std::uint32_t sector;
    bool dev;
};

}

[[nodiscard]] Ex<SystemArea> read_system_area(svc::IFile& file) {
    auto size = file.size();
    if (!size || size->v != kNddBytes) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    static constexpr std::uint8_t kRetailBad[] = {2, 3, 10, 11, 12, 16, 17, 18, 19, 20, 21, 22, 23};
    static constexpr std::uint8_t kDevBad[] = {0, 1, 8, 9, 16, 17, 18, 19, 20, 21, 22, 23};
    static constexpr Pass kPasses[] = {
        {{9, 8, 1, 0}, kSystemSectorBytes, false},
        {{11, 10, 3, 2}, kDevSystemSectorBytes, true}
    };
    SystemArea out{};
    Block b{};
    for (const Pass& pass : kPasses) {
        out.bad_lba.reset();
        bool found = false;
        for (const std::uint8_t blk : pass.blocks) {
            if (!read_exact(file, std::uint64_t{blk} * kZone0BlockBytes, b)) {
                return std::unexpected(Error{Errc::short_read, ERR_SITE(), blk});
            }
            if (valid_system_block(b, pass.sector, pass.dev)) {
                std::copy_n(b.begin(), out.sys.size(), out.sys.begin());
                out.disk_type = b[5] & 0x0F;
                found = true;
            } else {
                out.bad_lba.set(blk);
            }
        }
        if (!found)
            continue;
        for (const std::uint8_t lba : pass.dev ? std::span<const std::uint8_t>(kDevBad)
                                               : std::span<const std::uint8_t>(kRetailBad)) {
            out.bad_lba.set(lba);
        }
        auto id = read_disk_id(file, b, out);
        if (!id || !*id)
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 1});
        if (out.disk_type >= kDiskTypes) {
            return std::unexpected(Error{Errc::bad_format, ERR_SITE(), out.disk_type});
        }
        out.development = pass.dev;
        out.cart_expansion = !out.disk_id.empty() && out.disk_id.view()[0] == 'E';

        std::array<std::uint8_t, 4> sig{};
        if (pass.dev) {
            out.region = Region::Development;
        } else if (read_exact(file, 0, sig)) {
            if (sig == std::array<std::uint8_t, 4>{0xE8, 0x48, 0xD3, 0x16})
                out.region = Region::Japan;
            if (sig == std::array<std::uint8_t, 4>{0x22, 0x63, 0xEE, 0x56})
                out.region = Region::Usa;
        }
        return out;
    }
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 2});
}

}
