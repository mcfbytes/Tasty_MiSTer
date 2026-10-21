// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/subcode_sidecar.h"

#include <string>

#include "disc_lex.h"
#include "svc/track.h"

namespace mister::svc {

using namespace disc::detail;

Ex<std::size_t> SubcodeSidecar::read(Lba lba, std::span<std::byte> dst) {

    const std::uint32_t at = lba.v > sub_origin_ ? lba.v - sub_origin_ : 0u;
    if (sub_cdg_)
        return read_cdg_subcode_(at, dst);
    auto rd = sub_->read_at(
        static_cast<std::uint64_t>(at) * kSubcodeBytes,
        dst.subspan(0, kSubcodeBytes)
    );
    if (!rd)
        return std::unexpected(rd.error());
    if (*rd != kSubcodeBytes) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(*rd)}
        );
    }
    return kSubcodeBytes;
}

constexpr int kCdgColumnOffset[24] = {0,       18 - 24,  5 - 48,   23 - 72,  4 - 96,   2 - 120,
                                      6 - 144, 7 - 168,  8,        9 - 24,   10 - 48,  11 - 72,
                                      12 - 96, 13 - 120, 14 - 144, 15 - 168, 16,       17 - 24,
                                      1 - 48,  19 - 72,  20 - 96,  21 - 120, 22 - 144, 3 - 168};

constexpr std::uint32_t kCdgReach = 165;
constexpr std::size_t kCdgSpan = 254;

Ex<std::size_t> SubcodeSidecar::read_cdg_subcode_(std::uint32_t at, std::span<std::byte> dst) {

    const std::uint64_t want = static_cast<std::uint64_t>(at) * kSubcodeBytes;
    const std::uint64_t base = want > kCdgReach ? want - kCdgReach : 0;
    std::byte buf[kCdgSpan]{};
    auto rd = sub_->read_at(base, std::span<std::byte>(buf, sizeof(buf)));
    if (!rd)
        return std::unexpected(rd.error());
    for (std::size_t pack = 0; pack < 4; ++pack) {
        for (std::size_t col = 0; col < 24; ++col) {
            const int rel = static_cast<int>(pack * 24)
                + kCdgColumnOffset[col]
                + static_cast<int>(kCdgReach);
            dst[pack * 24 + col] = buf[static_cast<std::size_t>(rel)];
        }
    }
    return kSubcodeBytes;
}

SubcodeSidecar::Found SubcodeSidecar::open_sub_or_cdg(
    std::string_view cue_path,
    const DiscFileAccess& io
) {

    if (cue_path.size() < 4)
        return Found::None;
    std::string name(cue_path.substr(0, cue_path.size() - 4));
    name.append(".sub");
    auto s = io.open_file(name);
    bool cdg = false;
    if (!s) {
        name.replace(name.size() - 4, 4, ".cdg");
        s = io.open_file(name);
        cdg = true;
    }
    if (!s)
        return Found::None;
    sub_ = std::move(*s);
    sub_cdg_ = cdg;
    sub_origin_ = static_cast<std::uint32_t>(kRedBookPregap);
    return cdg ? Found::Cdg : Found::Sub;
}

}
