// SPDX-License-Identifier: GPL-3.0-or-later
#include "svc/filter_store.h"

#include "svc/filter_bank.h"
#include "svc/filter_send_plan.h"
#include "svc/filter_set.h"

#include <strings.h>

#include <cstdio>
#include <string>
#include <vector>

#include "svc/vfs.h"

namespace mister::svc {

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
    if (in_count == 0) return false;
    const int dup = static_cast<int>(kFilterPhases) / in_count;
    if (in_count * dup != static_cast<int>(kFilterPhases)) return false;
    for (int i = 0; i < in_count; ++i) {
        for (int j = 0; j < dup; ++j) {
            out[i * dup + j] = in[i];
        }
    }
    return true;
}

std::vector<std::byte> read_file(const Vfs& vfs, std::string_view path) {
    auto f = vfs.open(path, OpenMode::ReadWhole);
    if (!f) return {};
    auto sz = (*f)->size();
    if (!sz) return {};
    std::vector<std::byte> buf(static_cast<std::size_t>(sz->v));
    std::size_t off = 0;
    while (off < buf.size()) {
        auto n = (*f)->read_at(off, std::span<std::byte>(buf).subspan(off));
        if (!n || *n == 0) return {};
        off += *n;
    }
    return buf;
}

}  // namespace

bool same_digest(const FilterBank& a, const FilterBank& b) noexcept {
    if (a.digest_valid != b.digest_valid) return false;

    if (!a.digest_valid) return true;
    return a.is_adaptive == b.is_adaptive && a.phases == b.phases && a.adaptive == b.adaptive;
}

bool parse_video_filter(std::string_view text, FilterBank& out) {
    out = FilterBank{};

    std::array<FilterPhase, kFilterPhases * 2> phases{};
    int count = 0;
    bool is_adaptive = false;
    int scale = 2;

    std::string buf(text);
    buf.push_back('\0');
    LineCursor cursor{buf.data(), buf.data() + buf.size() - 1};
    while (const char* line = cursor.next()) {
        if (count == 0 && ::strcasecmp(line, "adaptive") == 0) {
            is_adaptive = true;
            continue;
        }
        if (count == 0 && ::strcasecmp(line, "10bit") == 0) {
            scale = 1;
            continue;
        }
        int phase[4];
        const int n = std::sscanf(line, "%d,%d,%d,%d", &phase[0], &phase[1], &phase[2], &phase[3]);
        if (n == 4) {

            if (count >= (is_adaptive ? static_cast<int>(kFilterPhases) * 2
                                      : static_cast<int>(kFilterPhases))) {
                return false;
            }
            for (std::size_t k = 0; k < 4; ++k) {
                phases[static_cast<std::size_t>(count)].t[k] =
                    static_cast<std::int16_t>(phase[k] * scale);
            }
            ++count;
        }
    }

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
    }
    out.digest_valid = true;
    return valid;
}

FilterBank nearest_neighbour_bank() {
    FilterBank b{};
    const FilterPhase nn[2] = {FilterPhase{{0, 256, 0, 0}}, FilterPhase{{0, 0, 256, 0}}};
    (void)scale_phases(b.phases.data(), nn, 2);
    b.digest_valid = true;
    return b;
}

std::string_view ScalerSlot::name_view() const noexcept {
    std::size_t n = 0;
    while (n < name.size() && name[n] != '\0')
        ++n;
    return {name.data(), n};
}

std::array<ScalerSlot, kScalerSlots> decode_scaler_cfg(std::span<const std::byte> blob,
                                                       const ScalerSeeds& seeds) {
    std::array<ScalerSlot, kScalerSlots> out{};
    if (!blob.empty()) {

        constexpr std::size_t kStride = 1 + kScalerNameCap;
        for (std::size_t s = 0; s < kScalerSlots; ++s) {
            const std::size_t base = s * kStride;
            if (base < blob.size()) out[s].mode = static_cast<std::uint8_t>(blob[base]);
            for (std::size_t i = 0; i < kScalerNameCap && base + 1 + i < blob.size(); ++i) {
                out[s].name[i] = static_cast<char>(blob[base + 1 + i]);
            }
        }

        if (out[0].mode > 1) out = {};
        return out;
    }
    const std::string_view sv[kScalerSlots] = {seeds.horz, seeds.vert, seeds.scan, seeds.ilace};
    for (std::size_t s = 0; s < kScalerSlots; ++s) {
        if (sv[s].empty()) continue;
        const std::size_t n = sv[s].size() < kScalerNameCap - 1 ? sv[s].size() : kScalerNameCap - 1;
        for (std::size_t i = 0; i < n; ++i)
            out[s].name[i] = sv[s][i];
        out[s].mode = 1;
    }
    return out;
}

void load_filter_set(const Vfs& vfs, std::string_view core, const ScalerSeeds& seeds,
                     FilterSet& out) {
    out = FilterSet{};

    std::string cfg_path{"config/"};
    cfg_path += core;
    cfg_path += "_scaler.cfg";
    const auto blob = read_file(vfs, cfg_path);
    const auto slots = decode_scaler_cfg(std::span<const std::byte>(blob), seeds);

    for (std::size_t s = 0; s < kScalerSlots; ++s) {

        std::string path{"filters/"};
        path += slots[s].name_view();
        if (path.size() > kScalerNameCap) path.resize(kScalerNameCap);
        const auto bytes = read_file(vfs, path);
        const std::string_view text =
            bytes.empty()
                ? std::string_view{}
                : std::string_view{reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        const bool ok = parse_video_filter(text, out.banks[s]);
        out.modes[s] = ok ? slots[s].mode : 0;
    }
}

std::uint16_t filter_bank_word(std::span<const FilterPhase> phases, std::uint16_t ver,
                               std::uint32_t bank, std::size_t i) noexcept {
    const std::uint32_t enc = ver & 0x3u;
    if (enc == 0u) return 0;
    if (enc == 1u) {

        const std::size_t iter = i / kFilterTaps;
        const std::size_t tap = i % kFilterTaps;
        const std::size_t addr = static_cast<std::size_t>(bank) * 64u + iter * kFilterTaps + tap;
        const int t = phases[iter * 16u].t[tap];
        return static_cast<std::uint16_t>((static_cast<std::uint32_t>(t >> 1) & 0x1FFu) |
                                          (static_cast<std::uint32_t>(addr) << 9));
    }

    const bool full = (ver & 0x4u) != 0;
    const std::size_t skip = full ? 1u : 4u;
    const int shift = full ? 0 : 1;
    const std::size_t base =
        static_cast<std::size_t>(bank) * (full ? kFilterPhases * kFilterTaps : 64u * kFilterTaps);
    const std::size_t pair = i / 2u;
    const std::size_t iter = pair / kFilterTaps;
    const std::size_t tap = pair % kFilterTaps;
    if ((i & 1u) == 0u) {
        return static_cast<std::uint16_t>(base + iter * kFilterTaps + tap);
    }
    const int t = phases[iter * skip].t[tap];
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(t >> shift) & 0x3FFu);
}

FilterSendPlan plan_filter_banks(const FilterBank& horiz, const FilterBank& vert, std::uint16_t ver,
                                 bool send_horiz, bool send_vert) noexcept {
    FilterSendPlan p{};
    if ((ver & 0x3u) == 0u) return p;
    if (send_horiz) p.segs[p.count++] = {false, false, 0};
    if (send_vert) p.segs[p.count++] = {true, false, 1};
    if ((ver & 0x3u) == 3u) {

        if (horiz.is_adaptive && send_horiz) {
            p.segs[p.count++] = {false, true, 2};
        } else if (vert.is_adaptive && send_vert) {
            p.segs[p.count++] = {true, true, 3};
        }
    }
    return p;
}

}  // namespace mister::svc
