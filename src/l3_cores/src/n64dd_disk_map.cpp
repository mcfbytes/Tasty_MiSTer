// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64dd_disk_map.h"

#include <algorithm>

#include "infra/indexed_array.h"

namespace mister::cores::n64dd {

Ex<void> DiskMap::build_into(DiskMap& m, const SystemArea& area) noexcept {
    m.count_ = 0;
    m.ram_count_ = 0;
    m.ram_ok_ = false;
    m.disk_type_ = area.disk_type;
    const SystemBlockView sys{area.sys};
    const DiskType type = area.disk_type;

    if (!type.in_range()) {
        return std::unexpected(type.refusal(Errc::bad_format, ERR_SITE()));
    }
    std::uint64_t source = 0;
    std::uint32_t lba = 0;
    std::uint32_t starting_block = 0;
    for (std::uint32_t vzone = 0; vzone < kZones; ++vzone) {
        const std::uint32_t pzone = infra::at_index(kVzoneToPzone, type)[vzone];
        const Zone& zone = kZoneTable[pzone];
        const std::uint32_t block_bytes = std::uint32_t{zone.sector_bytes} * kSectorsPerBlock;
        const std::uint32_t defect_start = pzone != 0 ? sys.defect_start(pzone) : 0u;
        const std::uint32_t defect_stop = sys.defect_stop(pzone);
        std::array<bool, 158> bad_track{};
        std::uint32_t listed = 0;
        for (std::uint32_t d = defect_start; d < defect_stop; ++d) {

            if (!sys.has_defect_entry(d)) {
                m.count_ = 0;
                return std::unexpected(Error{Errc::bad_format, ERR_SITE(), d});
            }
            const std::uint32_t t = sys.defect_entry(d);
            if (t < zone.tracks)
                bad_track[t] = true;
            ++listed;
        }
        for (std::uint32_t f = 0; listed < kBadTracksPerZone && f < zone.tracks; ++f) {
            bad_track[zone.tracks - 1u - f] = true;
            ++listed;
        }
        for (std::uint32_t zti = 0; zti < zone.tracks; ++zti) {
            const std::uint32_t zone_track = zone.head != 0 ? zone.tracks - 1u - zti : zti;
            const std::uint32_t track = zone.track_offset + zone_track;
            const std::uint16_t flat_track = static_cast<std::uint16_t>(track);
            const DiskHead head{zone.head};
            if (bad_track[zone_track]) {
                m.steps_[m.count_++] = Step{
                    .where = FlatBlock{DiskTrack{flat_track}, head, TrackBlock{}},
                    .zone_bytes = static_cast<std::uint8_t>(zone.sector_bytes)
                };
                continue;
            }
            for (std::uint32_t block = 0; block < 2; ++block) {
                if (lba >= kLbaCount) {
                    m.count_ = 0;
                    return std::unexpected(Error{Errc::bad_format, ERR_SITE(), lba});
                }
                const bool dev_system = area.development()
                    && std::ranges::find(kDevSystemLbas, lba) != kDevSystemLbas.end();
                const TrackBlock track_block{static_cast<std::uint8_t>(starting_block ^ block)};
                m.steps_[m.count_++] = Step{
                    .where = FlatBlock{DiskTrack{flat_track}, head, track_block},
                    .lba = NddLba{static_cast<std::uint16_t>(lba)},
                    .copy_bytes = static_cast<std::uint8_t>(
                        dev_system ? kDevSystemSectorBytes : zone.sector_bytes
                    ),
                    .zone_bytes = static_cast<std::uint8_t>(zone.sector_bytes),
                    .good = true,
                    .bad_lba = area.bad_lba.test(lba)
                };
                source += block_bytes;
                ++lba;
            }
            starting_block ^= 1u;
        }
    }
    if (lba != kLbaCount || source != kNddBytes) {
        m.count_ = 0;
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), lba});
    }

    const NddLba ram_start{infra::at_index(kRamStartLba, type)};
    std::uint64_t ram = 0;
    for (std::size_t i = 0; i < m.count_; ++i) {
        const Step& s = m.steps_[i];
        if (!s.good || !s.lba.at_or_above(ram_start))
            continue;
        m.ram_runs_[m.ram_count_] = RamRun{
            .base = static_cast<std::uint32_t>(ram),
            .step = static_cast<std::uint16_t>(i)
        };
        ++m.ram_count_;
        ram += std::uint64_t{s.zone_bytes} * kSectorsPerBlock;
    }
    const std::uint32_t ram_bytes = infra::at_index(kRamBytes, type);
    m.ram_ok_ = ram_bytes != 0 && ram == ram_bytes;
    return {};
}

std::size_t DiskMap::ram_index_at_(std::uint64_t ram_off) const noexcept {

    const auto* first = ram_runs_.data();
    const auto* last = first + ram_count_;
    const auto* it = std::upper_bound(first, last, ram_off, [](std::uint64_t v, const RamRun& r) {
        return v < r.base;
    });
    return it == first ? 0 : static_cast<std::size_t>(it - first) - 1;
}

}
