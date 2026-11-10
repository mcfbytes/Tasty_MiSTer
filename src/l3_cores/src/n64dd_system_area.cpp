// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64dd_system_area.h"

#include <algorithm>
#include <span>

#include "infra/byte_view.h"

namespace mister::cores::n64dd {
namespace {

using Block = std::array<std::uint8_t, kZone0BlockBytes>;

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
    const SystemBlockView blk{b};
    const bool retail = blk.is(kRetailJpnSig) || blk.is(kRetailUsaSig);
    const bool devm = blk.is(kDevSig);
    if (dev ? !devm : !retail)
        return false;
    if (blk.format() != kFormat)
        return false;
    if (!blk.type_in_range())
        return false;
    if (!blk.filler())
        return false;
    if (blk.terminator() != kTerminator)
        return false;
    return repeats(b, kSectorsPerBlock, sector);
}

[[nodiscard]] Ex<bool> read_disk_id(svc::IFile& f, Block& b, SystemArea& out) {
    bool valid = false;
    for (const std::uint32_t lba : {15u, 14u}) {
        const proto::FileSize at = proto::byte_offset(
            proto::Lba{lba},
            proto::BlockSize{kZone0BlockBytes}
        );
        if (!svc::read_exact(f, at, std::as_writable_bytes(std::span(b))))
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

std::string_view root_ipl_name(Region region) noexcept {
    switch (region) {
        case Region::Japan:
            return "boot3.rom";
        case Region::Usa:
            return "boot5.rom";
        case Region::Development:
            return "boot4.rom";
        case Region::Unknown:
            break;
    }

    return "boot3.rom";
}

[[nodiscard]] Ex<SystemArea> read_system_area(svc::IFile& file) {
    auto size = file.size();
    if (!size || *size != proto::FileSize{kNddBytes}) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    static constexpr std::uint8_t kRetailBad[] = {2, 3, 10, 11, 12, 16, 17, 18, 19, 20, 21, 22, 23};
    static constexpr std::uint8_t kDevBad[] = {0, 1, 8, 9, 16, 17, 18, 19, 20, 21, 22, 23};
    static constexpr Pass kPasses[] = {
        {{9, 8, 1, 0}, kSystemSectorBytes, false},
        {kDevSystemLbas, kDevSystemSectorBytes, true}
    };
    SystemArea out{};
    Block b{};
    for (const Pass& pass : kPasses) {
        out.bad_lba.reset();
        bool found = false;
        for (const std::uint8_t blk : pass.blocks) {
            const proto::FileSize at = proto::byte_offset(
                proto::Lba{blk},
                proto::BlockSize{kZone0BlockBytes}
            );
            if (auto r = svc::read_exact(file, at, std::as_writable_bytes(std::span(b))); !r) {
                return std::unexpected(r.error());
            }
            if (valid_system_block(b, pass.sector, pass.dev)) {
                const SystemBlockView view{b};
                const auto type = view.type();

                if (!type) {
                    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), view.type_byte()});
                }
                std::copy_n(b.begin(), out.sys.size(), out.sys.begin());
                out.disk_type = *type;
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
        out.cart_expansion = !out.disk_id.empty() && out.disk_id.view()[0] == 'E';

        std::array<std::uint8_t, kSignatureBytes> sig{};
        if (pass.dev) {
            out.region = Region::Development;
        } else if (
            svc::read_exact(file, proto::FileSize{}, std::as_writable_bytes(std::span(sig)))
        ) {
            if (signature_is(sig, kRetailJpnSig))
                out.region = Region::Japan;
            if (signature_is(sig, kRetailUsaSig))
                out.region = Region::Usa;
        }
        return out;
    }
    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 2});
}

}
