// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/cdi_disc_rules.h"

#include <array>
#include <cstring>
#include <optional>

#include "infra/byte_view.h"
#include "svc/disc_reader.h"
#include "svc/track.h"

namespace mister::cores::cdi {

BlockAnswer classify(HeadLba lba, const svc::DiscGeometry& disc) noexcept {
    const svc::Toc& toc = disc.toc;
    if (!lba.readable() || !disc.mounted || toc.last == svc::TrackCount{})
        return BlockAnswer::TocArea;
    for (svc::TrackIndex i{}; toc.last.contains(i); i = i + svc::TrackCount{1}) {
        const svc::Track& t = toc.at(i);
        const HeadLba lo = HeadLba::of(t.start) - SectorCount::of(t.pregap);
        const HeadLba hi = HeadLba::of(t.end) - SectorCount{1};
        if (lba >= lo && lba <= hi)
            return BlockAnswer::Track;
    }
    return BlockAnswer::LeadOut;
}

template <DiscType Previous>
proto::BlockSize CdiDiscRulesAfter<Previous>::block_size() const noexcept {
    return proto::BlockSize{kBlockSize32};
}

template <DiscType Previous>
void CdiDiscRulesAfter<Previous>::fill(
    const svc::DiscGeometry& disc,
    svc::IDiscReader& reader,
    proto::Lba lba,
    std::span<std::uint8_t> out
) const noexcept {
    std::memset(out.data(), 0, out.size());
    const auto sector = out.first<kSectorBytes>();
    const auto q = out.subspan<kSectorBytes, kQBytes>();
    const auto rw = out.subspan<kSectorBytes + kQBytes, kRwBytes>();
    const HeadLba at = HeadLba::of(lba);
    if (!at.readable()) {

        if (!disc.mounted || disc.toc.last == svc::TrackCount{})
            return;
        std::array<TocEntry, kTocEntries> rows{};
        const std::span<const TocEntry> toc_rows = build_toc(
            disc.toc,
            disc_type(disc.toc, Previous),
            rows
        );
        const QFrame frame = compose_toc_q(at, toc_rows);
        std::memcpy(q.data(), frame.bytes.data(), kQBytes);
        return;
    }
    switch (classify(at, disc)) {
        case BlockAnswer::TocArea:
        case BlockAnswer::LeadOut: {

            const QFrame frame = compose_q(at, disc.toc);
            std::memcpy(q.data(), frame.bytes.data(), kQBytes);
            return;
        }
        case BlockAnswer::Track:
            break;
    }
    std::array<std::byte, svc::kFullFrameBytes> frame{};
    const auto got = reader.read_form(svc::DiscForm::FullFrame, lba, frame);
    if (got) {

        std::memcpy(sector.data(), frame.data(), kSectorBytes);
        const auto bytes = infra::byte_cast<const std::uint8_t>(std::span(frame));
        widen_rw(bytes.template subspan<kSectorBytes, kRawSubcodeBytes>(), rw);
    }

    const QFrame q_frame = compose_q(at, disc.toc);
    std::memcpy(q.data(), q_frame.bytes.data(), kQBytes);
}

template class CdiDiscRulesAfter<DiscType::CdRom>;
template class CdiDiscRulesAfter<DiscType::CdRomXa>;

const svc::IDiscRules& rules_after(DiscType previous) noexcept {
    return previous == DiscType::CdRom ? static_cast<const svc::IDiscRules&>(kRulesAfterCdRom)
                                       : static_cast<const svc::IDiscRules&>(kRulesAfterXa);
}

}
