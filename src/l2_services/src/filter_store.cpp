// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/filter_store.h"

#include "svc/filter_bank.h"
#include "svc/filter_kernel.h"
#include "svc/filter_send_plan.h"
#include "svc/filter_set.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "infra/ascii.h"
#include "infra/byte_view.h"
#include "proto/link.h"
#include "svc/vfs.h"

namespace mister::svc {

Ex<void> ScalerSlotMode::write_to(proto::Link& link) const {
    return link.write_byte(v);
}

namespace {

struct LineCursor {
    char* pos;
    char* end;

    const char* next() {
        while (pos < end) {
            char* st = pos;
            while (pos < end && *pos != '\0' && *pos != '\r' && *pos != '\n')
                ++pos;
            *pos = '\0';
            while (*st == ' ' || *st == '\t')
                ++st;
            if (*st == '#' || *st == ';' || *st == '\0') {
                ++pos;
            } else {
                return st;
            }
        }
        return nullptr;
    }
};

bool scale_phases(FilterPhase* out, const FilterPhase* in, int in_count) {
    if (in_count == 0)
        return false;
    const int dup = static_cast<int>(kFilterPhases) / in_count;
    if (in_count * dup != static_cast<int>(kFilterPhases))
        return false;
    for (int i = 0; i < in_count; ++i) {
        for (int j = 0; j < dup; ++j) {
            out[i * dup + j] = in[i];
        }
    }
    return true;
}

std::vector<std::byte> read_file(const Vfs& vfs, std::string_view path) {
    auto f = vfs.open(path, OpenMode::ReadWhole);
    if (!f)
        return {};
    auto sz = (*f)->size();
    if (!sz)
        return {};
    std::vector<std::byte> buf(static_cast<std::size_t>(sz->v));
    std::size_t off = 0;
    while (off < buf.size()) {
        auto n = (*f)->read_at(off, std::span<std::byte>(buf).subspan(off));
        if (!n || *n == 0)
            return {};
        off += *n;
    }
    return buf;
}

}

bool same_digest(const FilterBank& a, const FilterBank& b) noexcept {
    if (a.digest_valid != b.digest_valid)
        return false;

    if (!a.digest_valid)
        return true;
    return a.is_adaptive == b.is_adaptive && a.phases == b.phases && a.adaptive == b.adaptive;
}

FilterParse parse_video_filter(std::string_view text) {
    FilterParse parsed{};

    std::array<FilterPhase, kFilterPhases * 2> phases{};
    int count = 0;
    bool is_adaptive = false;
    int scale = 2;

    std::string buf(text);
    buf.push_back('\0');
    LineCursor cursor{buf.data(), buf.data() + buf.size() - 1};
    while (const char* line = cursor.next()) {
        if (count == 0 && infra::ieq(line, "adaptive")) {
            is_adaptive = true;
            continue;
        }
        if (count == 0 && infra::ieq(line, "10bit")) {
            scale = 1;
            continue;
        }
        int phase[4];
        const int n = std::sscanf(line, "%d,%d,%d,%d", &phase[0], &phase[1], &phase[2], &phase[3]);
        if (n == 4) {

            if (count
                >= (is_adaptive ? static_cast<int>(kFilterPhases) * 2
                                : static_cast<int>(kFilterPhases))) {
                parsed.outcome = FilterParseOutcome::OverCap;
                return parsed;
            }
            for (std::size_t k = 0; k < 4; ++k) {
                phases[static_cast<std::size_t>(count)].t[k] = static_cast<std::int16_t>(
                    phase[k] * scale
                );
            }
            ++count;
        }
    }

    FilterBank& out = parsed.bank;
    bool valid = false;
    if (is_adaptive) {
        out.is_adaptive = true;
        valid = scale_phases(out.phases.data(), phases.data(), count / 2);
        valid = valid && scale_phases(out.adaptive.data(), phases.data() + count / 2, count / 2);
    } else if (count == 32) {

        valid = scale_phases(out.phases.data(), phases.data(), 16);
    } else {
        valid = scale_phases(out.phases.data(), phases.data(), count);
    }

    if (!valid) {
        out.is_adaptive = false;
        const FilterPhase nn[2] = {FilterPhase{{0, 256, 0, 0}}, FilterPhase{{0, 0, 256, 0}}};
        (void)scale_phases(out.phases.data(), nn, 2);
        parsed.outcome = FilterParseOutcome::NearestNeighbour;
    }
    out.digest_valid = true;
    return parsed;
}

FilterBank nearest_neighbour_bank() {
    FilterBank b{};
    const FilterPhase nn[2] = {FilterPhase{{0, 256, 0, 0}}, FilterPhase{{0, 0, 256, 0}}};
    (void)scale_phases(b.phases.data(), nn, 2);
    b.digest_valid = true;
    return b;
}

ScalerSlots<ScalerSlot> decode_scaler_cfg(
    std::span<const std::byte> blob,
    const ScalerSeeds& seeds
) {
    ScalerSlots<ScalerSlot> out{};
    if (!blob.empty()) {

        constexpr std::size_t kStride = 1 + kScalerNameCap;
        for (const ScalerSlotIndex slot : kScalerSlotOrder) {
            const std::size_t base = slot.offset(kStride);
            if (base < blob.size())
                out[slot].mode = ScalerSlotMode::of(blob[base]);
            const std::size_t name_at = base + 1;
            if (name_at >= blob.size())
                continue;

            const std::string_view field = infra::as_text(
                blob.subspan(name_at, std::min(kScalerNameCap, blob.size() - name_at))
            );
            (void)out[slot].name.assign(field.substr(0, field.find('\0')));
        }

        if (!out[ScalerSlotIndex::of(VFilter::Horz)].mode.legal())
            out = {};
        return out;
    }
    for (const ScalerSlotIndex slot : kScalerSlotOrder) {
        const std::string_view seed = seeds[slot];
        if (seed.empty())
            continue;
        (void)out[slot].name.assign(seed);
        out[slot].mode = ScalerSlotMode::from_file();
    }
    return out;
}

void load_filter_set(
    const Vfs& vfs,
    std::string_view core,
    const ScalerSeeds& seeds,
    FilterSet& out
) {
    out = FilterSet{};

    std::string cfg_path{"config/"};
    cfg_path += core;
    cfg_path += "_scaler.cfg";
    const auto blob = read_file(vfs, cfg_path);
    const auto slots = decode_scaler_cfg(std::span<const std::byte>(blob), seeds);

    for (const ScalerSlotIndex slot : kScalerSlotOrder) {

        std::string path{"filters/"};
        path += slots[slot].name.view();
        if (path.size() > kScalerNameCap)
            path.resize(kScalerNameCap);
        const auto bytes = read_file(vfs, path);
        const std::string_view text = bytes.empty() ? std::string_view{}
                                                    : infra::as_text(std::span(bytes));
        const FilterParse parsed = parse_video_filter(text);
        out.slots[slot].bank = parsed.bank;
        out.slots[slot].mode = parsed.parsed() ? slots[slot].mode : ScalerSlotMode::off();
    }
}

namespace {

struct StoredPhases {
    std::span<const FilterPhase> phases;

    [[nodiscard]] constexpr const std::array<std::int16_t, kFilterTaps>& taps(
        FilterPhaseIndex phase
    ) const noexcept {
        return phase.taps(phases);
    }
};

}

std::uint16_t filter_bank_word(
    std::span<const FilterPhase> phases,
    FilterFormat fmt,
    FilterWireBank bank,
    FilterBankWordIndex i
) noexcept {
    return detail::filter_word(fmt, StoredPhases{phases}, base_index(bank), i);
}

FilterSendPlan plan_filter_banks(
    const FilterBank& horiz,
    const FilterBank& vert,
    FilterFormat fmt,
    bool send_horiz,
    bool send_vert
) noexcept {
    FilterSendPlan p{};
    if (fmt.encoding == FilterEncoding::None)
        return p;
    if (send_horiz)
        p.segs[p.count++] = {FilterWireBank::Horiz};
    if (send_vert)
        p.segs[p.count++] = {FilterWireBank::Vert};
    if (fmt.encoding == FilterEncoding::WordsAdaptive) {

        if (horiz.is_adaptive && send_horiz) {
            p.segs[p.count++] = {FilterWireBank::AdaptiveHoriz};
        } else if (vert.is_adaptive && send_vert) {
            p.segs[p.count++] = {FilterWireBank::AdaptiveVert};
        }
    }
    return p;
}

}
