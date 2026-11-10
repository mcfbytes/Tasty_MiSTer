// SPDX-License-Identifier: GPL-3.0-or-later
#include "input_rules.h"

#include "infra/ascii.h"
#include "infra/indexed_array.h"
#include "infra/persist.h"
#include "svc/joy_bit.h"
#include "svc/map_store.h"
#include "svc/sys_button.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <charconv>
#include <cstddef>
#include <optional>
#include <utility>

namespace mister::svc {

namespace {

constexpr unsigned kHexNibbleBits = 4;
constexpr std::uint32_t kHexRadix = 16;
constexpr unsigned kHexDigitMask = 0xFu;
constexpr std::size_t kHexDigits16 = 4;
constexpr std::size_t kHexDigits32 = 8;

std::optional<std::uint32_t> hex_token(std::string_view t, std::size_t max_digits) {
    if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
        t.remove_prefix(2);
    if (t.empty() || t.size() > max_digits)
        return std::nullopt;
    std::uint32_t v = 0;
    for (const char c : t) {
        const char l = infra::ascii_lower(c);
        const int d = (c >= '0' && c <= '9') ? c - '0' : (l >= 'a' && l <= 'f') ? l - 'a' + 10 : -1;
        if (d < 0)
            return std::nullopt;
        v = v * kHexRadix + static_cast<std::uint32_t>(d);
    }
    return v;
}

std::optional<std::pair<bool, std::uint32_t>> vid_pid_token(std::string_view t) {
    if (t.empty())
        return std::nullopt;
    const char head = infra::ascii_lower(t[0]);
    if (head != 'v' && head != 'p')
        return std::nullopt;
    std::size_t i = 1;
    std::size_t n = 0;
    while (i < t.size() && infra::ascii_lower(t[i]) == 'i') {
        ++i;
        ++n;
    }
    if (n == 0)
        return std::nullopt;
    n = 0;
    while (i < t.size() && infra::ascii_lower(t[i]) == 'd') {
        ++i;
        ++n;
    }
    if (n == 0 || i >= t.size() || t[i] != ':')
        return std::nullopt;
    const auto v = hex_token(t.substr(i + 1), kHexDigits16);
    if (!v)
        return std::nullopt;
    return std::pair<bool, std::uint32_t>{head == 'v', *v};
}

}

namespace rules {
namespace {

std::string hex4(std::uint16_t v) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (int shift = static_cast<int>((kHexDigits16 - 1) * kHexNibbleBits); shift >= 0;
         shift -= static_cast<int>(kHexNibbleBits)) {
        out.push_back(kDigits[(static_cast<unsigned>(v) >> shift) & kHexDigitMask]);
    }
    return out;
}

std::string hex8(std::uint32_t v) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    for (int shift = static_cast<int>((kHexDigits32 - 1) * kHexNibbleBits); shift >= 0;
         shift -= static_cast<int>(kHexNibbleBits)) {
        out.push_back(kDigits[(v >> shift) & kHexDigitMask]);
    }
    return out;
}

std::string dec(std::uint32_t v) {
    char buf[16];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    return std::string(buf, static_cast<std::size_t>(r.ptr - buf));
}

std::string vid_pid(Vid vid, Pid pid) {
    std::string s = hex4(vid.v);
    s.push_back('_');
    s += hex4(pid.v);
    return s;
}

constexpr std::string_view kEventInfix = "/event";

std::optional<unsigned> event_number(std::string_view devname) {
    const auto pos = devname.find(kEventInfix);
    if (pos == std::string_view::npos)
        return std::nullopt;
    const char* first = devname.data() + pos + kEventInfix.size();
    const char* last = devname.data() + devname.size();
    unsigned n = 0;
    const auto r = std::from_chars(first, last, n);
    if (r.ec != std::errc{} || r.ptr == first)
        return std::nullopt;
    return n;
}

std::string_view basename_of(std::string_view path) {
    const auto pos = path.rfind('/');
    return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

std::string_view line_value(std::string_view line) {
    const auto pos = line.find('=');
    if (pos == std::string_view::npos)
        return {};
    return line.substr(pos + 1);
}

}

std::uint32_t str_hash(std::string_view s, std::uint32_t seed) {
    std::uint32_t hash = seed;
    for (const char c : s) {
        hash = hash * kDjb2Step + static_cast<std::uint32_t>(static_cast<unsigned char>(c));
    }
    return hash;
}

namespace {

struct IdstrUniqRule {
    Vid vid;
    Pid pid;
    bool teensy;
};

constexpr auto kIdstrUniqRules = std::to_array<IdstrUniqRule>({
    {.vid = Vid{0x2341}, .pid = kAnyPid, .teensy = false},
    {.vid = Vid{0x16C0}, .pid = kAnyPid, .teensy = true},
    {.vid = Vid{0x16D0}, .pid = Pid{0x127E}, .teensy = false},
    {.vid = Vid{0x16D0}, .pid = Pid{0x1460}, .teensy = false},
    {.vid = Vid{0x1209}, .pid = Pid{0x595A}, .teensy = false},
});

constexpr Vid kControllaVid{0x1209};
constexpr Pid kControllaPids[] = {Pid{0xFACE}, Pid{0xFACA}};

constexpr unsigned kTeensyPidHigh = 0x04;

constexpr bool is_teensy_pid(Pid pid) noexcept {
    return (pid.v >> 8) == kTeensyPidHigh;
}

constexpr bool wants_uniq_suffix(Vid vid, Pid pid) noexcept {
    for (const IdstrUniqRule& r : kIdstrUniqRules) {
        if (r.vid != vid)
            continue;
        if (r.teensy)
            return is_teensy_pid(pid);
        if (r.pid == kAnyPid || r.pid == pid)
            return true;
    }
    return false;
}

constexpr bool wants_name_sum(Vid vid, Pid pid) noexcept {
    if (vid != kControllaVid)
        return false;
    for (const Pid p : kControllaPids) {
        if (p == pid)
            return true;
    }
    return false;
}

constexpr bool idstr_rules_have_exact_vids() noexcept {
    for (const IdstrUniqRule& r : kIdstrUniqRules) {
        if (r.vid == kAnyVid)
            return false;
    }
    return true;
}

static_assert(idstr_rules_have_exact_vids(), "an idstr row's vid is EXACT");

}

void derive_idstr(DeviceFacts& slot) {
    const auto id_stem = [&] {
        return vid_pid(slot.id.vid, slot.id.pid);
    };

    if (wants_uniq_suffix(slot.id.vid, slot.id.pid) && !slot.uniq.empty()) {
        std::string s = id_stem();
        s.push_back('_');
        s.append(slot.uniq);
        for (char& c : s) {
            if (c == '/' || c == ' ' || c == '*' || c == ':')
                c = '_';
        }
        slot.id.id = std::move(s);
        slot.id.source = IdSource::VidPidUniq;

        slot.id.name = slot.uniq;
        return;
    }

    if (wants_name_sum(slot.id.vid, slot.id.pid)) {
        std::uint32_t sum = 0;
        const std::size_t n = slot.id.name.size() < kNameSumChars ? slot.id.name.size()
                                                                  : kNameSumChars;
        for (std::size_t i = 0; i < n; ++i) {
            sum += static_cast<unsigned char>(slot.id.name[i]);
        }
        std::string s = id_stem();
        s.push_back('_');
        s += dec(sum);
        slot.id.id = std::move(s);
        slot.id.source = IdSource::VidPidNameSum;
        return;
    }

    slot.id.id = id_stem();
    slot.id.source = IdSource::VidPid;
}

bool wants_unique_mapping(const DeviceIdentity& id, const UniquePolicy& pol) {
    const VidPidKey device = VidPidKey::of(id.vid, id.pid);
    for (const VidPidKey row : pol.vidpids) {
        if (row == kVidPidEndRow)
            break;
        if (pol.force || pol.all || row == VidPidKey::every_device() || row == device)
            return true;
    }
    return false;
}

std::string unique_mapping(const DeviceIdentity& id, const UniquePolicy& pol) {
    if (!wants_unique_mapping(id, pol))
        return id.id;
    std::string s = id.id;
    s.push_back('_');
    s += hex8(id.unique_hash);
    return s;
}

unsigned apply_proc_devices(std::string_view proc_text, FactsSpan slots) {
    for (DeviceFacts* const sp : slots)
        sp->id.merge_id.clear();

    std::string phys;
    std::string uniq;

    std::string sysfs;
    unsigned resolved = 0;

    std::size_t pos = 0;
    while (pos <= proc_text.size()) {
        std::size_t eol = proc_text.find('\n', pos);
        if (eol == std::string_view::npos)
            eol = proc_text.size();
        std::string_view line = proc_text.substr(pos, eol - pos);
        pos = eol + 1;
        while (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);

        if (line.empty()) {
            phys.clear();
            uniq.clear();
            if (eol == proc_text.size())
                break;
            continue;
        }

        if (line.starts_with("P: Phys"))
            phys = line_value(line);
        if (line.starts_with("U: Uniq"))
            uniq = line_value(line);
        if (line.starts_with("S: Sysfs"))
            sysfs = line_value(line);
        if (line.starts_with("H: ")) {
            std::string id;
            if (!phys.empty() && !uniq.empty()) {
                id = phys;
                id.push_back('/');
                id.append(uniq);
            } else if (!phys.empty()) {
                id = phys;
            } else {
                id = uniq;
            }

            const std::string_view handlers = line_value(line);
            if (!handlers.empty() && !id.empty()) {
                for (DeviceFacts* const sp : slots) {
                    DeviceFacts& s = *sp;

                    std::string needle(basename_of(s.devname));
                    needle.push_back(' ');
                    if (handlers.find(needle) == std::string_view::npos)
                        continue;
                    s.id.merge_id = id;
                    s.uniq = uniq;
                    s.mac = uniq;
                    s.sysfs = sysfs;
                    s.id.unique_hash = str_hash(s.id.merge_id);
                    s.id.unique_hash = str_hash(uniq, s.id.unique_hash);
                    ++resolved;
                }
            }
        }
        if (eol == proc_text.size())
            break;
    }
    return resolved;
}

bool bt_eligible(const DeviceFacts& slot) noexcept {
    return !slot.mac.empty() && slot.sysfs.find("bluetooth") != std::string::npos;
}

void make_unique(FactsSpan slots, const DeviceMatch& match) {
    unsigned cnt = 0;
    std::optional<unsigned> last;
    while (true) {
        std::size_t idx = slots.size();
        std::optional<unsigned> next;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (!match.matches(slots[i]->id.vid, slots[i]->id.pid))
                continue;
            const std::optional<unsigned> num = event_number(slots[i]->devname);
            if (!num || (last && *num <= *last))
                continue;
            if (!next || *num < *next) {
                next = num;
                idx = i;
            }
        }
        if (idx == slots.size())
            break;
        last = next;
        slots[idx]->id.merge_id.push_back('/');
        slots[idx]->id.merge_id += dec(cnt);
        slots[idx]->id.port_suffix = static_cast<std::uint8_t>(cnt);
        ++cnt;
    }
}

void apply_no_merge(FactsSpan slots, std::span<const NoMergeRule> extra) {
    for (const NoMergeRule& r : no_merge_rules())
        make_unique(slots, r.match);

    for (const NoMergeRule& r : extra)
        make_unique(slots, r.match);
}

bool is_mouse_node(std::string_view devname) {
    return basename_of(devname).starts_with("mouse");
}

MergeStats merge_by_id(FactsSpan slots) {
    MergeStats st{};
    for (std::size_t i = 0; i < slots.size(); ++i) {
        slots[i]->bind = static_cast<unsigned>(i);
        if (slots[i]->id.merge_id.empty() || slots[i]->mouse)
            continue;
        for (std::size_t j = 0; j < i; ++j) {
            if (slots[i]->id.merge_id != slots[j]->id.merge_id)
                continue;
            slots[i]->bind = static_cast<unsigned>(j);
            ++st.merged;
            break;
        }
    }
    return st;
}

MergeStats fixup_mouse_nodes(FactsSpan slots, const SpinnerPolicy& spin) {
    MergeStats st{};
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i]->mouse)
            continue;
        for (std::size_t j = 0; j < slots.size(); ++j) {
            if (slots[j]->mouse)
                continue;
            if (slots[i]->id.merge_id != slots[j]->id.merge_id)
                continue;

            if (slots[i]->id.merge_id.empty()) {
                ++st.empty_id_skips;
                break;
            }
            slots[i]->bind = static_cast<unsigned>(j);
            slots[i]->inherit_identity_from(*slots[j]);
            ++st.mouse_fixups;

            if (slots[i]->quirk == QuirkId::None) {
                if (spin.claims(slots[i]->id.vid, slots[i]->id.pid)) {
                    slots[i]->quirk = QuirkId::Mssp;
                    slots[i]->bind = static_cast<unsigned>(i);
                    slots[i]->spinner_prediv = 1;
                }
                if (const auto fx = find_mouse_fixup(slots[i]->id.vid, slots[i]->id.pid)) {
                    slots[i]->quirk = fx->quirk;
                    slots[i]->bind = static_cast<unsigned>(i);
                    slots[i]->spinner_prediv = 3;
                }
                if (slots[i]->quirk == QuirkId::Mssp) {
                    slots[i]->id.merge_id.append(kSpinnerSuffix);
                    ++st.mssp;
                }
            }
            break;
        }
    }
    return st;
}

MergeStats resolve_identities(
    FactsSpan slots,
    std::string_view proc_text,
    std::span<const NoMergeRule> extra,
    const SpinnerPolicy& spin
) {
    apply_proc_devices(proc_text, slots);
    apply_no_merge(slots, extra);
    MergeStats st = merge_by_id(slots);
    const MergeStats fx = fixup_mouse_nodes(slots, spin);
    st.mouse_fixups = fx.mouse_fixups;
    st.empty_id_skips = fx.empty_id_skips;
    st.mssp = fx.mssp;
    return st;
}

std::string map_filename(std::string_view core, const DeviceIdentity& id, MapKind kind, bool mod) {
    const MapKindSpec& spec = map_spec(kind);
    std::string s;
    if (spec.core_prefix && !core.empty()) {
        s.append(core);
        s.push_back('_');
    }
    s.append(spec.infix);
    if (spec.stem == StemSource::VidPid) {
        s.append(vid_pid(id.vid, id.pid));
    } else {
        s.append(id.id);
        if (id.unique_filenames) {
            s.push_back('_');
            s += hex8(id.unique_hash);
        }
    }
    if (spec.mod_suffix && mod)
        s.append("_m");
    s.append(spec.suffix);
    return s;
}

MapPaths map_paths(std::string_view name, MapKind kind) {
    MapPaths p{};

    switch (map_spec(kind).paths) {
        case PathPolicy::InputsThenConfig:
            p.primary = "inputs/";
            p.fallback = name;
            break;
        case PathPolicy::InputsOnly:
            p.primary = "inputs/";
            break;
        case PathPolicy::ConfigOnly:
            break;
    }
    p.primary.append(name);
    return p;
}

constexpr float kDiagonalSlopeMin = 0.85f;
constexpr float kBoxRadiusDiag = 1.4142136f;
constexpr float kBoxRadiusDiagScale = 2.4142136f;
constexpr float kLearnedMaxFloorFactor = 2.0f;
constexpr float kAxisScale = static_cast<float>(AnalogXy::kAxisMax);

constexpr Deadzone kDeadzoneCheapBound{2};

bool is_diagonal(AnalogXy in) {
    if (in.x == 0 || in.y == 0)
        return false;
    if (in.x == in.y || in.x == -in.y)
        return true;
    const AnalogXy::Axis dom = dominant_axis(in);
    const auto num = component(in, other_axis(dom));
    const auto den = component(in, dom);
    return std::fabs(num / den) >= kDiagonalSlopeMin;
}

AnalogXy apply_deadzone(AnalogXy in, const DeadzoneParams& p) {
    const auto x = component(in, AnalogXy::Axis::X);
    const auto y = component(in, AnalogXy::Axis::Y);
    const float dz = p.deadzone.magnitude();

    if (p.deadzone <= kDeadzoneCheapBound) {
        if (p.deadzone != kDeadzoneNone) {
            if (p.deadzone.swallows(magnitude_cardinal(in)))
                return AnalogXy{0, 0};
        }
        return in;
    }

    const float radius = std::hypot(x, y);
    if (radius <= dz)
        return AnalogXy{0, 0};

    const float angle = std::atan2(y, x);
    const float box_radius = 1.0f
        / std::fmax(std::fabs(std::sin(angle)), std::fabs(std::cos(angle)));
    const float cardinality = (kBoxRadiusDiag - box_radius) * kBoxRadiusDiagScale;

    const float learned_cardinal = p.learned.max_cardinal;
    const float learned_range = p.learned.max_range;
    const float max_cardinal = learned_cardinal > (kLearnedMaxFloorFactor * dz) ? learned_cardinal
                                                                                : kAxisScale;
    const float max_diagonal = learned_range > (kLearnedMaxFloorFactor * dz) ? learned_range
                                                                             : kAxisScale;
    const float range = cardinality * max_cardinal + (1.0f - cardinality) * max_diagonal;

    const float weight = 1.0f - std::fmax(range - radius, 0.0f) / (range - dz);
    const float adjusted = std::fmin(weight * range, max_cardinal * box_radius);

    if (adjusted > radius)
        return in;

    float ox = std::nearbyint(adjusted * std::cos(angle));
    float oy = std::nearbyint(adjusted * std::sin(angle));
    const auto lo = static_cast<float>(p.min_out);
    ox = std::fmin(std::fmax(ox, lo), kAxisScale);
    oy = std::fmin(std::fmax(oy, lo), kAxisScale);
    return AnalogXy{static_cast<std::int32_t>(ox), static_cast<std::int32_t>(oy)};
}

namespace {

constexpr std::uint32_t kDefMmap[kJoyWords] = {
    0x00000321u, 0x00000320u, 0x00000323u, 0x00000322u, 0x00000131u, 0x00000130u, 0x00000133u,
    0x00000134u, 0x00000136u, 0x00000137u, 0x0000013Au, 0x0000013Bu, 0u,          0u,
    0u,          0u,          0u,          0u,          0u,          0u,          0u,
    0x0000013Cu, 0x0000013Cu, 0x01300131u, 0x00020000u, 0x00020001u, 0x00020003u, 0x00020004u,
    0x00020000u, 0x00020001u, 0u,          0u,
};
static_assert(sizeof kDefMmap == sizeof(SysWords), "req: the system map is exactly 128 bytes");

struct RouteRow {
    SysButton sys;
    JoyBit wire;
};

constexpr RouteRow kDirectionRoute[] = {
    {kSysBtnRight, kJoyBitRight},
    {kSysBtnLeft, kJoyBitLeft},
    {kSysBtnDown, kJoyBitDown},
    {kSysBtnUp, kJoyBitUp},
};

constexpr RouteRow kMenuRoute[] = {
    {kSysBtnY, kJoyBitBtn3},
    {kSysBtnX, kJoyBitBtn4},
    {kSysBtnL, kJoyBitL},
    {kSysBtnR, kJoyBitR},
    {kSysBtnStart, kJoyBitL2},
    {kSysBtnSelect, kJoyBitR2},
};

void copy_low_halves(
    const SysWords& sys_map,
    std::span<const RouteRow> rows,
    WireWords& w
) noexcept {
    for (const RouteRow& row : rows)
        w[row.wire].lo = sys_map[row.sys].lo;
}

void apply_axis_edges(
    const SysWords& sys_map,
    SysButton axis_word,
    JoyBit min_edge,
    JoyBit max_edge,
    WireWords& w
) noexcept {
    const PackedKeys word = sys_map[axis_word];
    if (word == PackedKeys{})
        return;
    const EvdevCode axis = word.lo;
    w[min_edge].hi = axis.key_emu_min();
    w[max_edge].hi = axis.key_emu_max();
}

constexpr SysWords default_words() noexcept {
    SysWords out{};
    SysButton word{};
    for (const std::uint32_t packed : kDefMmap) {
        out[word] = packed_keys_of(packed);
        word = word.next();
    }
    return out;
}
}

SysWords default_sys_map() {
    return default_words();
}

WireWords derive_joy_map(const SysWords& sys_map, bool axis_emu, const JoyPlan& plan) {
    WireWords w{};

    copy_low_halves(sys_map, kDirectionRoute, w);

    if (axis_emu) {
        apply_axis_edges(sys_map, kSysBtnAxisX, kJoyBitLeft, kJoyBitRight, w);
        apply_axis_edges(sys_map, kSysBtnAxisY, kJoyBitUp, kJoyBitDown, w);
    }

    for (JoyBit bit = kJoyBitBtn1; bit < kJoyBitEnd; bit = bit.next()) {
        const std::optional<SysButton> src = infra::at_index(plan.src, bit);
        if (!src)
            continue;
        w[bit] = sys_map[*src];
    }
    return w;
}

WireWords derive_menu_map(const SysWords& sys_map, bool axis_keys) {
    WireWords w{};

    copy_low_halves(sys_map, kDirectionRoute, w);

    const PackedKeys menu = sys_map[kSysBtnMenuFunc];
    w[kJoyBitBtn1].lo = menu.lo.is_unset() ? sys_map[kSysBtnA].lo : menu.lo;
    w[kJoyBitBtn2].lo = menu.hi.is_unset() ? sys_map[kSysBtnB].lo : menu.hi;
    for (const RouteRow& row : kMenuRoute)
        w[row.wire].lo = sys_map[row.sys].lo;
    if (axis_keys) {
        apply_axis_edges(sys_map, kSysBtnAxisX, kJoyBitLeft, kJoyBitRight, w);
        apply_axis_edges(sys_map, kSysBtnAxisY, kJoyBitUp, kJoyBitDown, w);
    }
    return w;
}

JoyMask mask_for(std::span<const PackedKeys> joy_map, const KeyDownSet<kDownWords>& down) {
    JoyMask mask{};
    JoyBit bit{};
    for (const PackedKeys packed : joy_map.first(std::min(joy_map.size(), kJoyWords))) {
        if (packed != PackedKeys{}) {
            for (const EvdevCode code : {packed.lo, packed.hi}) {
                if (code.is_unset())
                    continue;
                if (down.has(code)) {
                    mask = mask.with(bit.mask());
                    break;
                }
            }
        }
        bit = bit.next();
    }
    return mask;
}

Deadzone deadzone_class(QuirkId quirk) {
    switch (quirk) {
        case QuirkId::LightGun:
        case QuirkId::OpenFire:
        case QuirkId::Wheel:
            return kDeadzoneNone;
        case QuirkId::Ds3:
        case QuirkId::Ds4:
            return kDeadzoneDualShock;
        case QuirkId::None:
        case QuirkId::Mssp:
        case QuirkId::AtariVcs:
        case QuirkId::Paddle:
        case QuirkId::Spinner:
        case QuirkId::Ds4Touch:
            break;
    }
    return kDeadzoneDefault;
}

bool deadzone_uid_matches(std::string_view uid, const DeviceFacts& dev) {
    if (const auto v = hex_token(uid, kHexDigits32)) {

        return VidPidKey::of(dev.id.vid, dev.id.pid) == VidPidKey{*v};
    }
    if (const auto vp = vid_pid_token(uid)) {
        return vp->first ? dev.id.vid.v == static_cast<std::uint16_t>(vp->second)
                         : dev.id.pid.v == static_cast<std::uint16_t>(vp->second);
    }

    return infra::icontains(dev.id.merge_id, uid)
        || infra::icontains(dev.sysfs, uid)
        || infra::icontains(dev.id.id, uid);
}

Deadzone deadzone_for(const DeviceFacts& dev, std::span<const DeadzoneRule> cfg) {
    for (const DeadzoneRule& r : cfg) {
        if (deadzone_uid_matches(r.uid.view(), dev))
            return r.deadzone;
    }
    return deadzone_class(dev.quirk);
}

}

}
