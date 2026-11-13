// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/psx_disc_rules.h"

#include <algorithm>
#include <cstring>
#include <string_view>

#include "cores/psx_core.h"
#include "cores/psx_sector_map.h"
#include "infra/byte_view.h"
#include "svc/track.h"

namespace mister::cores {
namespace {

constexpr proto::Lba kRootFolderLba{150 + 22};
constexpr proto::Lba kLicenseLba{154};

constexpr proto::BlockCount kRootDirSectors{25};

inline constexpr std::string_view
    kLicenceText = "          Licensed  by          Sony Computer Entertainment ";
static_assert(kLicenceText.size() == 60);

struct PrefixRegion {
    std::string_view prefix;
    svc::DiscRegion region;
};

constexpr PrefixRegion kPrefixTable[] = {
    {"SCES", svc::DiscRegion::Europe},
    {"SLES", svc::DiscRegion::Europe},
    {"SCUS", svc::DiscRegion::Usa},
    {"SLUS", svc::DiscRegion::Usa},
    {"SCPM", svc::DiscRegion::Japan},
    {"SLPM", svc::DiscRegion::Japan},
    {"SCPS", svc::DiscRegion::Japan},
    {"SLPS", svc::DiscRegion::Japan},
    {"SIPS", svc::DiscRegion::Japan},
    {"PUPX", svc::DiscRegion::Usa},
    {"PEPX", svc::DiscRegion::Europe},
    {"PAPX", svc::DiscRegion::Japan},
    {"PCPX", svc::DiscRegion::Japan},
    {"SCZS", svc::DiscRegion::Japan},
    {"SCED", svc::DiscRegion::Europe},
    {"SLED", svc::DiscRegion::Europe},
};

constexpr std::string_view kIdTerminator = ";1";

struct LicenceTail {
    std::string_view text;
    svc::DiscRegion region;
};

constexpr LicenceTail kLicenceTails[] = {
    {"Amer  ica ", svc::DiscRegion::Usa},
    {"Inc.", svc::DiscRegion::Japan},
    {"Euro pe", svc::DiscRegion::Europe},
};

std::span<const std::uint8_t> find_bytes(
    std::span<const std::uint8_t> hay,
    std::string_view needle
) {
    const std::size_t at = infra::as_text(hay).find(needle);
    if (at == std::string_view::npos)
        return {};
    return hay.subspan(at);
}

svc::DiscRegion licence_region(std::span<const std::uint8_t> sector) {
    const std::span<const std::uint8_t> lic = find_bytes(sector, kLicenceText);
    if (lic.empty())
        return svc::DiscRegion::Unknown;
    const std::string_view tail = infra::as_text(lic).substr(kLicenceText.size());
    for (const LicenceTail& row : kLicenceTails) {
        if (tail.starts_with(row.text))
            return row.region;
    }
    return svc::DiscRegion::Unknown;
}

}

proto::BlockSize PsxDiscRules::block_size() const noexcept {
    return proto::BlockSize{svc::kRawFrameBytes};
}

void PsxDiscRules::fill(
    const svc::DiscGeometry& disc,
    svc::IDiscReader& reader,
    proto::Lba lba,
    std::span<std::uint8_t> out
) const noexcept {
    using Answer = PsxSectorMap::Answer;
    const Answer a = PsxSectorMap{disc}.classify(lba);
    if (a != Answer::Engine) {
        std::memset(out.data(), PsxSectorMap::fill_byte(a), out.size());
        return;
    }
    std::memset(out.data(), PsxSectorMap::fill_byte(Answer::Engine), out.size());

    (void)reader.read_form(svc::DiscForm::RawFrame, lba, infra::byte_cast<std::byte>(out));
}

svc::DiscIdentity PsxDiscRules::identify(
    const svc::DiscGeometry& disc,
    svc::IDiscReader& reader
) const noexcept {
    PsxGameId id;
    svc::DiscRegion prefix_region = svc::DiscRegion::Unknown;

    const proto::Lba root_end = kRootFolderLba + kRootDirSectors;
    for (proto::Lba at = kRootFolderLba; at < root_end; at += proto::BlockCount{1}) {
        std::uint8_t sector[svc::kRawFrameBytes];
        fill(disc, reader, at, std::span<std::uint8_t>{sector, svc::kRawFrameBytes});
        std::span<const std::uint8_t> start{};
        for (const PrefixRegion& row : kPrefixTable) {
            prefix_region = row.region;
            start = find_bytes(
                std::span<const std::uint8_t>{sector, svc::kRawFrameBytes},
                row.prefix
            );
            if (!start.empty())
                break;
        }
        if (start.empty())
            continue;
        const std::span<const std::uint8_t> end = find_bytes(start, kIdTerminator);
        if (end.empty())
            continue;
        id.assign(start, start.size() - end.size());
        break;
    }
    if (id.empty())
        prefix_region = svc::DiscRegion::Unknown;

    std::uint8_t sector[svc::kRawFrameBytes];
    fill(disc, reader, kLicenseLba, std::span<std::uint8_t>{sector, svc::kRawFrameBytes});
    const svc::DiscRegion licence = licence_region(
        std::span<const std::uint8_t>{sector, svc::kRawFrameBytes}
    );
    svc::DiscIdentity out{};
    out.id = id.text;
    out.region = licence != svc::DiscRegion::Unknown ? licence : prefix_region;
    return out;
}

}
