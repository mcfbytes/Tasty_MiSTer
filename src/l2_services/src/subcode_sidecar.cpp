// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/subcode_sidecar.h"

#include <string>

#include "disc_lex.h"
#include "svc/track.h"

namespace mister::svc {

using namespace disc::detail;

namespace {

constexpr proto::BlockSize kRecordBytes{kSubcodeBytes};

}

Ex<std::span<std::byte>> SubcodeSidecar::read(Lba lba, std::span<std::byte> dst) {

    const BlockCount at = record_at(lba);
    if (sub_cdg_)
        return read_cdg_subcode_(at, dst);
    auto rd = sub_->read_at(
        kRecordBytes.position_of(proto::Lba{} + at),
        dst.subspan(0, kSubcodeBytes)
    );
    if (!rd)
        return std::unexpected(rd.error());
    if (*rd != kSubcodeBytes) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(*rd)}
        );
    }
    return dst.first(kSubcodeBytes);
}

constexpr int kCdgColumnOffset[] = {0,       18 - 24,  5 - 48,   23 - 72,  4 - 96,   2 - 120,
                                    6 - 144, 7 - 168,  8,        9 - 24,   10 - 48,  11 - 72,
                                    12 - 96, 13 - 120, 14 - 144, 15 - 168, 16,       17 - 24,
                                    1 - 48,  19 - 72,  20 - 96,  21 - 120, 22 - 144, 3 - 168};

constexpr std::uint32_t kCdgReach = 165;
constexpr std::size_t kCdgSpan = 254;

Ex<std::span<std::byte>> SubcodeSidecar::read_cdg_subcode_(
    BlockCount at,
    std::span<std::byte> dst
) {
    constexpr std::size_t kCdgColumns = std::size(kCdgColumnOffset);
    constexpr std::size_t kCdgPacks = kSubcodeBytes / kCdgColumns;

    const std::uint64_t want = kRecordBytes.position_of(proto::Lba{} + at);
    const std::uint64_t base = want > kCdgReach ? want - kCdgReach : 0;
    std::byte buf[kCdgSpan]{};
    auto rd = sub_->read_at(base, std::span<std::byte>(buf, sizeof(buf)));
    if (!rd)
        return std::unexpected(rd.error());
    for (std::size_t pack = 0; pack < kCdgPacks; ++pack) {
        for (std::size_t col = 0; col < kCdgColumns; ++col) {
            const std::ptrdiff_t rel = static_cast<std::ptrdiff_t>(pack * kCdgColumns + kCdgReach)
                + kCdgColumnOffset[col];
            dst[pack * kCdgColumns + col] = buf[static_cast<std::size_t>(rel)];
        }
    }
    return dst.first(kSubcodeBytes);
}

SubcodeSidecar::Found SubcodeSidecar::open_sub_or_cdg(
    std::string_view cue_path,
    const DiscFileAccess& io
) {

    if (cue_path.size() < kExtCue.size())
        return Found::None;
    std::string name(cue_path.substr(0, cue_path.size() - kExtCue.size()));
    name.append(kExtSub);
    auto s = io.open_file(name);
    bool cdg = false;
    if (!s) {
        name.replace(name.size() - kExtSub.size(), kExtSub.size(), kExtCdg);
        s = io.open_file(name);
        cdg = true;
    }
    if (!s)
        return Found::None;
    sub_ = std::move(*s);
    sub_cdg_ = cdg;
    sub_origin_ = Lba{0} + kRedBookPregap;
    return cdg ? Found::Cdg : Found::Sub;
}

}
