// SPDX-License-Identifier: GPL-3.0-or-later
#include "input_rules.h"

#include "infra/persist.h"

#include <array>
#include <cmath>
#include <charconv>
#include <cstddef>
#include <optional>
#include <utility>

namespace mister::svc {

namespace {

constexpr NoMergeRule kNoMergeRules[] = {

    {DeviceMatch{Vid{0x289B}, Pid{0x0057}, MatchArity::VidNotPid},
     "req row 1: Raphnet, all PIDs EXCEPT 0x0057"},
    {DeviceMatch{Vid{0x0E8F}, Pid{0x3013}, MatchArity::VidAndPid},
     "req row 2: Mayflash SNES 2-port adapter"},
    {DeviceMatch{Vid{0x16C0}, Pid{0x05E1}, MatchArity::VidAndPid},
     "req row 3: XinMo XM-10 2-player encoder"},
    {DeviceMatch{Vid{0x045E}, Pid{0x02A1}, MatchArity::VidAndPid},
     "req row 4: Xbox 360 wireless receiver"},
    {DeviceMatch{Vid{0x8282}, Pid{0x3201}, MatchArity::VidAndPid},
     "req row 5: Irken JAMMA Expander / Mojo Retro"},
    {DeviceMatch{Vid{0x1209}, Pid{0xFACA}, MatchArity::VidAndPid}, "req row 6: ControllaBLE"},
    {DeviceMatch{Vid{0x16D0}, Pid{0x127E}, MatchArity::VidAndPid},
     "req row 7: Reflex Adapt to USB"},
    {DeviceMatch{Vid{0x16D0}, Pid{0x1460}, MatchArity::VidAndPid},
     "req row 8: Reflex Adapt Classic2USB"},
    {DeviceMatch{Vid{0x1209}, Pid{0x595A}, MatchArity::VidAndPid}, "req row 9: RetroZord adapter"},
};

constexpr MouseFixupRule kMouseFixupRules[] = {

    {DeviceMatch{Vid{0x32BE}, Pid{0x1420}, MatchArity::VidAndPid}, QuirkId::Mssp,
     "req Arcade Spinner TS-BSP01/Atari, prediv 3"},
};

consteval bool quirk_rows_are_vid_exact() {
    for (const NoMergeRule& r : kNoMergeRules) {
        if (r.match.vid == kAnyVid) return false;
    }
    for (const MouseFixupRule& r : kMouseFixupRules) {
        if (r.match.vid == kAnyVid) return false;
    }
    return true;
}
static_assert(quirk_rows_are_vid_exact(),
              "a quirk row's vid is EXACT at every MatchArity — kAnyVid is the "
              "ADMISSION table's notation only, and a zero-VID quirk row would "
              "match every /dev/input/mouseN node (req)");

constexpr AdmissionRule kAdmissionRules[] = {

    {kAnyVid, kAnyPid, "MiSTer virtual input", NameMatch::Exact, AdmitAction::Reject, QuirkId::None,
     "input.cpp:5207-5213 — skip our own uinput node (UINPUT_NAME)"},
    {Vid{0x054C}, kAnyPid, "Motion", NameMatch::Contains, AdmitAction::Reject, QuirkId::None,
     "input.cpp:5268-5273 — DualShock/DualSense Motion Sensors: don't use Accelerometer"},

    {Vid{0x054C}, Pid{0x05C4}, "Touchpad", NameMatch::Contains, AdmitAction::Assign,
     QuirkId::Ds4Touch, "input.cpp:5280-5283 — DS4 v1 touchpad node"},
    {Vid{0x054C}, Pid{0x09CC}, "Touchpad", NameMatch::Contains, AdmitAction::Assign,
     QuirkId::Ds4Touch, "input.cpp:5280-5283 — DS4 v2 touchpad node"},
    {Vid{0x054C}, Pid{0x0BA0}, "Touchpad", NameMatch::Contains, AdmitAction::Assign,
     QuirkId::Ds4Touch, "input.cpp:5280-5283 — DS4 USB dongle touchpad node"},
    {Vid{0x054C}, Pid{0x0CE6}, "Touchpad", NameMatch::Contains, AdmitAction::Assign,
     QuirkId::Ds4Touch, "input.cpp:5280-5283 — DualSense touchpad node (THE rig device)"},
    {Vid{0x054C}, Pid{0x0268}, nullptr, NameMatch::Any, AdmitAction::Assign, QuirkId::Ds3,
     "input.cpp:5276 — DualShock 3"},
    {Vid{0x054C}, Pid{0x05C4}, nullptr, NameMatch::Any, AdmitAction::Assign, QuirkId::Ds4,
     "input.cpp:5278-5279 — DS4 v1"},
    {Vid{0x054C}, Pid{0x09CC}, nullptr, NameMatch::Any, AdmitAction::Assign, QuirkId::Ds4,
     "input.cpp:5278-5279 — DS4 v2"},
    {Vid{0x054C}, Pid{0x0BA0}, nullptr, NameMatch::Any, AdmitAction::Assign, QuirkId::Ds4,
     "input.cpp:5278-5279 — DS4 USB dongle"},
    {Vid{0x054C}, Pid{0x0CE6}, nullptr, NameMatch::Any, AdmitAction::Assign, QuirkId::Ds4,
     "input.cpp:5278-5279 — DualSense"},
    {Vid{0x057E}, Pid{0x0306}, "Accelerometer", NameMatch::Contains, AdmitAction::Reject,
     QuirkId::None, "input.cpp:5295-5300 — Wiimote accelerometer node"},
    {Vid{0x057E}, Pid{0x0330}, "Accelerometer", NameMatch::Contains, AdmitAction::Reject,
     QuirkId::None, "input.cpp:5295-5300 — Wii U pro accelerometer node"},
    {Vid{0x057E}, Pid{0x0306}, "Motion Plus", NameMatch::Contains, AdmitAction::Reject,
     QuirkId::None, "input.cpp:5302-5308 — Wiimote Motion Plus node"},
    {Vid{0x057E}, Pid{0x0330}, "Motion Plus", NameMatch::Contains, AdmitAction::Reject,
     QuirkId::None, "input.cpp:5302-5308 — Wii U pro Motion Plus node"},

    {Vid{0x057E}, kAnyPid, " IMU", NameMatch::ContainsCs, AdmitAction::Reject, QuirkId::None,
     "input.cpp:5322-5327 — Nintendo IMU node"},
    {Vid{0x3250}, Pid{0x1001}, nullptr, NameMatch::Any, AdmitAction::Assign, QuirkId::AtariVcs,
     "input.cpp:5484-5490 — Atari VCS joystick; `vcs_proc` is unported (item)"},
};

constexpr char lower_ascii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool contains_sub(std::string_view hay, std::string_view needle, bool fold) {
    if (needle.empty()) return true;
    if (hay.size() < needle.size()) return false;
    const std::size_t last = hay.size() - needle.size();
    for (std::size_t i = 0; i <= last; ++i) {
        std::size_t k = 0;
        for (; k < needle.size(); ++k) {
            char a = hay[i + k];
            char b = needle[k];
            if (fold) {
                a = lower_ascii(a);
                b = lower_ascii(b);
            }
            if (a != b) break;
        }
        if (k == needle.size()) return true;
    }
    return false;
}

std::optional<std::uint32_t> hex_token(std::string_view t, std::size_t max_digits) {
    if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) t.remove_prefix(2);
    if (t.empty() || t.size() > max_digits) return std::nullopt;
    std::uint32_t v = 0;
    for (const char c : t) {
        const char l = lower_ascii(c);
        const int d = (c >= '0' && c <= '9') ? c - '0' : (l >= 'a' && l <= 'f') ? l - 'a' + 10 : -1;
        if (d < 0) return std::nullopt;
        v = v * 16u + static_cast<std::uint32_t>(d);
    }
    return v;
}

std::optional<std::pair<bool, std::uint32_t>> vid_pid_token(std::string_view t) {
    if (t.empty()) return std::nullopt;
    const char head = lower_ascii(t[0]);
    if (head != 'v' && head != 'p') return std::nullopt;
    std::size_t i = 1;
    std::size_t n = 0;
    while (i < t.size() && lower_ascii(t[i]) == 'i') {
        ++i;
        ++n;
    }
    if (n == 0) return std::nullopt;
    n = 0;
    while (i < t.size() && lower_ascii(t[i]) == 'd') {
        ++i;
        ++n;
    }
    if (n == 0 || i >= t.size() || t[i] != ':') return std::nullopt;
    const auto v = hex_token(t.substr(i + 1), 4);
    if (!v) return std::nullopt;
    return std::pair<bool, std::uint32_t>{head == 'v', *v};
}

}  // namespace

std::span<const NoMergeRule> no_merge_rules() { return kNoMergeRules; }

std::optional<MouseFixupRule> find_mouse_fixup(Vid vid, Pid pid) {
    for (const MouseFixupRule& r : kMouseFixupRules) {
        if (r.match.matches(vid, pid)) return r;
    }
    return std::nullopt;
}

std::span<const AdmissionRule> admission_rules() { return kAdmissionRules; }

Admission admit_node(const NodeFacts& f) {
    for (const AdmissionRule& r : admission_rules()) {
        if (r.vid != kAnyVid && r.vid != f.vid) continue;
        if (r.pid != kAnyPid && r.pid != f.pid) continue;
        switch (r.name_match) {
            case NameMatch::Any:
                break;
            case NameMatch::Exact:
                if (f.name != std::string_view(r.name)) continue;
                break;
            case NameMatch::Contains:
                if (!contains_sub(f.name, r.name, true)) continue;
                break;
            case NameMatch::ContainsCs:
                if (!contains_sub(f.name, r.name, false)) continue;
                break;
        }
        if (r.action == AdmitAction::Reject) return Admission{false, QuirkId::None};
        return Admission{true, r.quirk};
    }
    return Admission{true, QuirkId::None};
}

namespace rules {
namespace {

void append_hex4(std::string& out, std::uint16_t v) {
    static constexpr char kDigits[] = "0123456789abcdef";
    for (int shift = 12; shift >= 0; shift -= 4) {
        out.push_back(kDigits[(static_cast<unsigned>(v) >> shift) & 0xFu]);
    }
}

void append_hex8(std::string& out, std::uint32_t v) {
    static constexpr char kDigits[] = "0123456789abcdef";
    for (int shift = 28; shift >= 0; shift -= 4) {
        out.push_back(kDigits[(v >> shift) & 0xFu]);
    }
}

void append_dec(std::string& out, std::uint32_t v) {
    char buf[16];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    out.append(buf, static_cast<std::size_t>(r.ptr - buf));
}

std::string vid_pid(Vid vid, Pid pid) {
    std::string s;
    append_hex4(s, vid.v);
    s.push_back('_');
    append_hex4(s, pid.v);
    return s;
}

long event_number(std::string_view devname) {
    const auto pos = devname.find("/event");
    if (pos == std::string_view::npos) return -1;
    const char* first = devname.data() + pos + 6;
    const char* last = devname.data() + devname.size();
    unsigned long n = 0;
    const auto r = std::from_chars(first, last, n);
    if (r.ec != std::errc{} || r.ptr == first) return -1;
    return static_cast<long>(n);
}

std::string_view basename_of(std::string_view path) {
    const auto pos = path.rfind('/');
    return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

std::string_view line_value(std::string_view line) {
    const auto pos = line.find('=');
    if (pos == std::string_view::npos) return {};
    return line.substr(pos + 1);
}

}  // namespace

std::uint32_t str_hash(std::string_view s, std::uint32_t seed) {
    std::uint32_t hash = seed;
    for (const char c : s) {
        hash = hash * 33u + static_cast<std::uint32_t>(static_cast<unsigned char>(c));
    }
    return hash;
}

void derive_idstr(DeviceSlot& slot) {
    const std::uint16_t vid = slot.id.vid.v;
    const std::uint16_t pid = slot.id.pid.v;
    const auto id_stem = [&] { return vid_pid(slot.id.vid, slot.id.pid); };

    const bool rule1_vidpid = vid == 0x2341 || (vid == 0x16C0 && (pid >> 8) == 0x04) ||
                              (vid == 0x16D0 && (pid == 0x127E || pid == 0x1460)) ||
                              (vid == 0x1209 && pid == 0x595A);
    if (rule1_vidpid && !slot.uniq.empty()) {
        std::string s = id_stem();
        s.push_back('_');
        s.append(slot.uniq);
        for (char& c : s) {
            if (c == '/' || c == ' ' || c == '*' || c == ':') c = '_';
        }
        slot.id.id = std::move(s);
        slot.id.source = IdSource::VidPidUniq;

        slot.id.name = slot.uniq;
        return;
    }

    if (vid == 0x1209 && (pid == 0xFACE || pid == 0xFACA)) {
        std::uint32_t sum = 0;
        const std::size_t n = slot.id.name.size() < 128u ? slot.id.name.size() : 128u;
        for (std::size_t i = 0; i < n; ++i) {
            sum += static_cast<unsigned char>(slot.id.name[i]);
        }
        std::string s = id_stem();
        s.push_back('_');
        append_dec(s, sum);
        slot.id.id = std::move(s);
        slot.id.source = IdSource::VidPidNameSum;
        return;
    }

    slot.id.id = id_stem();
    slot.id.source = IdSource::VidPid;
}

bool wants_unique_mapping(const DeviceIdentity& id, const UniquePolicy& pol) {
    const std::uint32_t vidpid =
        (static_cast<std::uint32_t>(id.vid.v) << 16) | static_cast<std::uint32_t>(id.pid.v);
    for (const std::uint32_t row : pol.vidpids) {
        if (row == 0) break;
        if (pol.force || pol.all || row == 1 || row == vidpid) return true;
    }
    return false;
}

std::string unique_mapping(const DeviceIdentity& id, const UniquePolicy& pol) {
    if (!wants_unique_mapping(id, pol)) return id.id;
    std::string s = id.id;
    s.push_back('_');
    append_hex8(s, id.unique_hash);
    return s;
}

unsigned apply_proc_devices(std::string_view proc_text, std::span<DeviceSlot> slots) {
    for (DeviceSlot& s : slots)
        s.id.merge_id.clear();

    std::string phys;
    std::string uniq;

    std::string sysfs;
    unsigned resolved = 0;

    std::size_t pos = 0;
    while (pos <= proc_text.size()) {
        std::size_t eol = proc_text.find('\n', pos);
        if (eol == std::string_view::npos) eol = proc_text.size();
        std::string_view line = proc_text.substr(pos, eol - pos);
        pos = eol + 1;
        while (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);

        if (line.empty()) {
            phys.clear();
            uniq.clear();
            if (eol == proc_text.size()) break;
            continue;
        }

        if (line.starts_with("P: Phys")) phys = line_value(line);
        if (line.starts_with("U: Uniq")) uniq = line_value(line);
        if (line.starts_with("S: Sysfs")) sysfs = line_value(line);
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
                for (DeviceSlot& s : slots) {

                    std::string needle(basename_of(s.devname));
                    needle.push_back(' ');
                    if (handlers.find(needle) == std::string_view::npos) continue;
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
        if (eol == proc_text.size()) break;
    }
    return resolved;
}

bool bt_eligible(const DeviceSlot& slot) noexcept {
    return !slot.mac.empty() && slot.sysfs.find("bluetooth") != std::string::npos;
}

void make_unique(std::span<DeviceSlot> slots, const DeviceMatch& match) {
    unsigned cnt = 0;
    long lastmin = -1;
    while (true) {
        std::size_t idx = slots.size();
        long min = -1;
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (!match.matches(slots[i].id.vid, slots[i].id.pid)) continue;
            const long num = event_number(slots[i].devname);
            if (num < 0 || num <= lastmin) continue;
            if (idx == slots.size() || num < min) {
                min = num;
                idx = i;
            }
        }
        if (idx == slots.size()) break;
        lastmin = min;
        slots[idx].id.merge_id.push_back('/');
        append_dec(slots[idx].id.merge_id, cnt);
        slots[idx].id.port_suffix = static_cast<std::uint8_t>(cnt);
        ++cnt;
    }
}

void apply_no_merge(std::span<DeviceSlot> slots, std::span<const NoMergeRule> extra) {
    for (const NoMergeRule& r : no_merge_rules())
        make_unique(slots, r.match);

    for (const NoMergeRule& r : extra)
        make_unique(slots, r.match);
}

bool is_mouse_node(std::string_view devname) { return basename_of(devname).starts_with("mouse"); }

MergeStats merge_by_id(std::span<DeviceSlot> slots) {
    MergeStats st{};
    for (std::size_t i = 0; i < slots.size(); ++i) {
        slots[i].bind = static_cast<unsigned>(i);
        if (slots[i].id.merge_id.empty() || slots[i].mouse) continue;
        for (std::size_t j = 0; j < i; ++j) {
            if (slots[i].id.merge_id != slots[j].id.merge_id) continue;
            slots[i].bind = static_cast<unsigned>(j);
            ++st.merged;
            break;
        }
    }
    return st;
}

MergeStats fixup_mouse_nodes(std::span<DeviceSlot> slots, const SpinnerPolicy& spin) {
    MergeStats st{};
    for (std::size_t i = 0; i < slots.size(); ++i) {
        if (!slots[i].mouse) continue;
        for (std::size_t j = 0; j < slots.size(); ++j) {
            if (slots[j].mouse) continue;
            if (slots[i].id.merge_id != slots[j].id.merge_id) continue;

            if (slots[i].id.merge_id.empty()) {
                ++st.empty_id_skips;
                break;
            }
            slots[i].bind = static_cast<unsigned>(j);
            slots[i].id.vid = slots[j].id.vid;
            slots[i].id.pid = slots[j].id.pid;
            slots[i].id.version = slots[j].id.version;
            slots[i].id.bustype = slots[j].id.bustype;
            slots[i].id.source = slots[j].id.source;
            slots[i].quirk = slots[j].quirk;
            slots[i].id.name = slots[j].id.name;
            slots[i].id.id = slots[j].id.id;
            ++st.mouse_fixups;

            if (slots[i].quirk == QuirkId::None) {
                const bool all_mice = spin.vid == kAllMiceVid && spin.pid == kAllMicePid;
                const bool exact = spin.vid != kAnyVid && spin.pid != kAnyPid &&
                                   slots[i].id.vid == spin.vid && slots[i].id.pid == spin.pid;
                if (all_mice || exact) {
                    slots[i].quirk = QuirkId::Mssp;
                    slots[i].bind = static_cast<unsigned>(i);
                    slots[i].spinner_prediv = 1;
                }
                if (const auto fx = find_mouse_fixup(slots[i].id.vid, slots[i].id.pid)) {
                    slots[i].quirk = fx->quirk;
                    slots[i].bind = static_cast<unsigned>(i);
                    slots[i].spinner_prediv = 3;
                }
                if (slots[i].quirk == QuirkId::Mssp) {
                    slots[i].id.merge_id.append("_sp");
                    ++st.mssp;
                }
            }
            break;
        }
    }
    return st;
}

MergeStats resolve_identities(std::span<DeviceSlot> slots, std::string_view proc_text,
                              std::span<const NoMergeRule> extra, const SpinnerPolicy& spin) {
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
            append_hex8(s, id.unique_hash);
        }
    }
    if (spec.mod_suffix && mod) s.append("_m");
    s.append(spec.suffix);
    return s;
}

MapPaths map_paths(std::string_view name, MapKind kind) {
    const PathPolicy policy = map_spec(kind).paths;
    MapPaths p{};
    if (policy != PathPolicy::ConfigOnly) p.primary = "inputs/";
    p.primary.append(name);
    if (policy == PathPolicy::InputsThenConfig) p.fallback = name;
    return p;
}

bool is_diagonal(AnalogXy in) {
    if (in.x == 0 || in.y == 0) return false;
    if (in.x == in.y || in.x == -in.y) return true;
    const bool x_dominant = (in.x > in.y) == (in.x > -in.y);
    const auto num = static_cast<float>(x_dominant ? in.y : in.x);
    const auto den = static_cast<float>(x_dominant ? in.x : in.y);
    return std::fabs(num / den) >= 0.85f;
}

AnalogXy apply_deadzone(AnalogXy in, const DeadzoneParams& p) {
    const auto x = static_cast<float>(in.x);
    const auto y = static_cast<float>(in.y);
    const auto dz = static_cast<float>(p.deadzone);

    if (p.deadzone <= 2) {
        if (p.deadzone != 0) {
            const std::int32_t dominant = ((in.x > in.y) == (in.x > -in.y)) ? in.x : in.y;
            const std::int32_t mag = dominant < 0 ? -dominant : dominant;
            if (mag <= p.deadzone) return AnalogXy{0, 0};
        }
        return in;
    }

    const float radius = std::hypot(x, y);
    if (radius <= dz) return AnalogXy{0, 0};

    const float angle = std::atan2(y, x);
    const float box_radius =
        1.0f / std::fmax(std::fabs(std::sin(angle)), std::fabs(std::cos(angle)));
    const float cardinality = (1.4142136f - box_radius) * 2.4142136f;

    const float max_cardinal = p.max_cardinal > (2.0f * dz) ? p.max_cardinal : 127.0f;
    const float max_diagonal = p.max_range > (2.0f * dz) ? p.max_range : 127.0f;
    const float range = cardinality * max_cardinal + (1.0f - cardinality) * max_diagonal;

    const float weight = 1.0f - std::fmax(range - radius, 0.0f) / (range - dz);
    const float adjusted = std::fmin(weight * range, max_cardinal * box_radius);

    if (adjusted > radius) return in;

    float ox = std::nearbyint(adjusted * std::cos(angle));
    float oy = std::nearbyint(adjusted * std::sin(angle));
    const auto lo = static_cast<float>(p.min_out);
    ox = std::fmin(std::fmax(ox, lo), 127.0f);
    oy = std::fmin(std::fmax(oy, lo), 127.0f);
    return AnalogXy{static_cast<std::int32_t>(ox), static_cast<std::int32_t>(oy)};
}

namespace {
constexpr std::uint32_t kDefMmap[32] = {
    0x00000321u, 0x00000320u, 0x00000323u, 0x00000322u, 0x00000131u, 0x00000130u, 0x00000133u,
    0x00000134u, 0x00000136u, 0x00000137u, 0x0000013Au, 0x0000013Bu, 0u,          0u,
    0u,          0u,          0u,          0u,          0u,          0u,          0u,
    0x0000013Cu, 0x0000013Cu, 0x01300131u, 0x00020000u, 0x00020001u, 0x00020003u, 0x00020004u,
    0x00020000u, 0x00020001u, 0u,          0u,
};
static_assert(sizeof kDefMmap == 128, "req: the Joystick blob is exactly 128 bytes");

constexpr std::size_t kBtnRight = 0;
constexpr std::size_t kBtnLeft = 1;
constexpr std::size_t kBtnDown = 2;
constexpr std::size_t kBtnUp = 3;
constexpr std::size_t kAxisX = 28;
constexpr std::size_t kAxisY = 29;

constexpr std::uint32_t kKeyEmu = 768;

ButtonMap encode_map(const std::uint32_t (&w)[32]) {
    ButtonMap m;
    m.kind = MapKind::Joystick;
    m.blob.resize(128);
    for (std::size_t i = 0; i < 32; ++i) {
        for (std::size_t b = 0; b < 4; ++b) {
            m.blob[i * 4 + b] = static_cast<std::byte>((w[i] >> (8u * b)) & 0xFFu);
        }
    }
    return m;
}
}  // namespace

ButtonMap default_sys_map() { return encode_map(kDefMmap); }

ButtonMap derive_joy_map(std::span<const std::uint32_t> sys_map, bool axis_emu,
                         const JoyPlan& plan) {
    std::uint32_t w[32] = {};

    if (sys_map.size() < 32) return encode_map(w);

    w[kBtnRight] = sys_map[kBtnRight] & 0xFFFFu;
    w[kBtnLeft] = sys_map[kBtnLeft] & 0xFFFFu;
    w[kBtnDown] = sys_map[kBtnDown] & 0xFFFFu;
    w[kBtnUp] = sys_map[kBtnUp] & 0xFFFFu;

    if (sys_map[kAxisX] != 0 && axis_emu) {
        const std::uint32_t key = kKeyEmu + ((sys_map[kAxisX] & 0xFFFFu) << 1u);
        w[kBtnLeft] = (key << 16u) | w[kBtnLeft];
        w[kBtnRight] = ((key + 1u) << 16u) | w[kBtnRight];
    }
    if (sys_map[kAxisY] != 0 && axis_emu) {
        const std::uint32_t key = kKeyEmu + ((sys_map[kAxisY] & 0xFFFFu) << 1u);
        w[kBtnUp] = (key << 16u) | w[kBtnUp];
        w[kBtnDown] = ((key + 1u) << 16u) | w[kBtnDown];
    }

    for (std::size_t j = 4; j < 32u; ++j) {
        const std::int8_t s = plan.src[j];
        if (s < 4 || s > 11) continue;
        w[j] = sys_map[static_cast<std::size_t>(s)];
    }
    return encode_map(w);
}

std::array<std::uint32_t, 32> derive_menu_map(std::span<const std::uint32_t> sys_map,
                                              bool axis_keys) {
    std::array<std::uint32_t, 32> w{};
    if (sys_map.size() < 32) return w;
    for (std::size_t i = 0; i < 4; ++i)
        w[i] = sys_map[i] & 0xFFFFu;
    const std::uint32_t ok = sys_map[23] & 0xFFFFu;
    const std::uint32_t esc = sys_map[23] >> 16;
    w[4] = ok != 0 ? ok : (sys_map[4] & 0xFFFFu);
    w[5] = esc != 0 ? esc : (sys_map[5] & 0xFFFFu);
    w[6] = sys_map[7] & 0xFFFFu;
    w[7] = sys_map[6] & 0xFFFFu;
    w[10] = sys_map[8] & 0xFFFFu;
    w[11] = sys_map[9] & 0xFFFFu;
    w[12] = sys_map[11] & 0xFFFFu;
    w[13] = sys_map[10] & 0xFFFFu;
    if (axis_keys) {
        if (sys_map[kAxisX] != 0) {
            const std::uint32_t key = kKeyEmu + ((sys_map[kAxisX] & 0xFFFFu) << 1u);
            w[kBtnLeft] |= key << 16u;
            w[kBtnRight] |= (key + 1u) << 16u;
        }
        if (sys_map[kAxisY] != 0) {
            const std::uint32_t key = kKeyEmu + ((sys_map[kAxisY] & 0xFFFFu) << 1u);
            w[kBtnUp] |= key << 16u;
            w[kBtnDown] |= (key + 1u) << 16u;
        }
    }
    return w;
}

JoyMask mask_for(std::span<const std::uint32_t> joy_map, std::span<const std::uint64_t> down_bits) {
    std::uint32_t mask = 0;
    for (std::size_t i = 0; i < joy_map.size() && i < 32u; ++i) {
        const std::uint32_t packed = joy_map[i];
        if (packed == 0) continue;
        const std::uint32_t lo = packed & 0xFFFFu;
        const std::uint32_t hi = packed >> 16;
        for (const std::uint32_t code : {lo, hi}) {
            if (code == 0) continue;
            const std::size_t word = code / 64u;
            if (word >= down_bits.size()) continue;
            if ((down_bits[word] >> (code % 64u)) & 1u) {
                mask |= (1u << i);
                break;
            }
        }
    }
    return JoyMask{mask};
}

std::uint8_t deadzone_class(QuirkId quirk) {
    switch (quirk) {
        case QuirkId::LightGun:
        case QuirkId::OpenFire:
        case QuirkId::Wheel:
            return 0;
        case QuirkId::Ds3:
        case QuirkId::Ds4:
            return 10;
        case QuirkId::None:
        case QuirkId::Mssp:
        case QuirkId::AtariVcs:
        case QuirkId::Paddle:
        case QuirkId::Spinner:
        case QuirkId::Ds4Touch:
            break;
    }
    return 2;
}

bool deadzone_uid_matches(std::string_view uid, Vid vid, Pid pid, std::string_view id,
                          std::string_view sysfs, std::string_view merge_id) {
    if (const auto v = hex_token(uid, 8)) {
        return ((static_cast<std::uint32_t>(vid.v) << 16) | pid.v) == *v;
    }
    if (const auto vp = vid_pid_token(uid)) {
        return vp->first ? vid.v == static_cast<std::uint16_t>(vp->second)
                         : pid.v == static_cast<std::uint16_t>(vp->second);
    }

    return contains_sub(merge_id, uid, true) || contains_sub(sysfs, uid, true) ||
           contains_sub(id, uid, true);
}

std::uint8_t deadzone_for(QuirkId quirk, Vid vid, Pid pid, std::string_view id,
                          std::string_view sysfs, std::string_view merge_id,
                          std::span<const DeadzoneRule> cfg) {
    for (const DeadzoneRule& r : cfg) {
        if (deadzone_uid_matches(r.uid.view(), vid, pid, id, sysfs, merge_id)) return r.deadzone;
    }
    return deadzone_class(quirk);
}

}  // namespace rules

std::optional<DeadzoneRule> parse_deadzone_rule(std::string_view line) {

    const auto is_sep = [](char c) { return c == ' ' || c == '\t' || c == ','; };
    std::size_t i = 0;
    while (i < line.size() && !is_sep(line[i]))
        ++i;
    if (i == 0 || i > kDeadzoneUidChars) return std::nullopt;
    std::size_t j = i;
    while (j < line.size() && is_sep(line[j]))
        ++j;
    if (j == i) return std::nullopt;
    const std::size_t digits = j;
    std::uint32_t value = 0;
    while (j < line.size() && line[j] >= '0' && line[j] <= '9') {
        value = value < 0x1000u ? value * 10u + static_cast<std::uint32_t>(line[j] - '0') : value;
        ++j;
    }
    if (j == digits || j != line.size()) return std::nullopt;
    DeadzoneRule r{};
    if (!r.uid.assign(line.substr(0, i))) return std::nullopt;
    r.deadzone = static_cast<std::uint8_t>(value > 64u ? 64u : value);
    return r;
}

std::vector<DeadzoneRule> parse_deadzone_rules(const ConfigSnapshot& cfg) {
    std::vector<DeadzoneRule> out;
    for (const auto& row : cfg.controller_deadzone) {
        const std::string_view line{static_cast<const char*>(row)};
        if (line.empty()) break;
        if (auto r = parse_deadzone_rule(line)) out.push_back(*r);
    }
    return out;
}

namespace {

constexpr std::size_t kMaxJoyButtons = 28;

constexpr std::size_t kNameCap = 32;

using NameBuf = std::array<char, kNameCap>;

NameBuf substr_field(std::string_view s, std::size_t idx) noexcept {
    NameBuf out{};
    std::size_t field = 0;
    std::size_t n = 0;
    for (const char c : s) {
        if (c == ',') {
            if (field == idx) break;
            ++field;
            continue;
        }
        if (field == idx && n + 1 < kNameCap) out[n++] = c;
    }
    out[n] = '\0';
    return out;
}

std::size_t buf_len(const NameBuf& b) noexcept {
    std::size_t n = 0;
    while (n + 1 < kNameCap && b[n] != '\0')
        ++n;
    return n;
}

std::string_view buf_view(const NameBuf& b) noexcept {
    return std::string_view(b.data(), buf_len(b));
}

void trim_buf(NameBuf& b) noexcept {
    std::size_t len = buf_len(b);
    while (len > 0 && b[len - 1] == ' ')
        b[--len] = '\0';
    std::size_t lead = 0;
    while (lead < len && b[lead] == ' ')
        ++lead;
    if (lead == 0) return;
    for (std::size_t i = 0; i + lead <= len; ++i)
        b[i] = b[i + lead];
}

bool has_char(std::string_view s, char c) noexcept { return s.find(c) != std::string_view::npos; }

bool ieq(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char ca = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] + 32) : a[i];
        const char cb = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] + 32) : b[i];
        if (ca != cb) return false;
    }
    return true;
}

bool istarts(std::string_view s, std::string_view pre) noexcept {
    return s.size() >= pre.size() && ieq(s.substr(0, pre.size()), pre);
}

int is_fire(std::string_view name) noexcept {
    if (!istarts(name, "fire") && !istarts(name, "button")) return 0;
    if (ieq(name, "fire") || has_char(name, '1')) return 1;
    if (has_char(name, '2')) return 2;
    if (has_char(name, '3')) return 3;
    if (has_char(name, '4')) return 4;
    return 0;
}

std::int8_t sys_for_button_name(std::string_view n) noexcept {
    constexpr std::int8_t kA = 4, kB = 5, kX = 6, kY = 7;
    constexpr std::int8_t kL = 8, kR = 9, kSelect = 10, kStart = 11;
    if (ieq(n, "A") || ieq(n, "Jump") || is_fire(n) == 1) return kA;
    if (ieq(n, "B") || is_fire(n) == 2) return kB;
    if (ieq(n, "X") || ieq(n, "C") || is_fire(n) == 3) return kX;
    if (ieq(n, "Y") || ieq(n, "D") || is_fire(n) == 4) return kY;
    if (ieq(n, "R") || ieq(n, "RT") || ieq(n, "Coin")) return kR;
    if (ieq(n, "L") || ieq(n, "LT")) return kL;
    if (ieq(n, "Select") || ieq(n, "Mode") || ieq(n, "Game Select") || ieq(n, "Start 2P"))
        return kSelect;
    if (ieq(n, "Start") || ieq(n, "Run") || ieq(n, "Pause") || ieq(n, "Start 1P")) return kStart;
    return JoyPlan::kNone;
}

}  // namespace

JoyPlan build_joy_plan(std::string_view j_list, std::string_view jn_list, std::string_view jp_list,
                       bool gamepad_defaults) noexcept {
    JoyPlan plan{};

    std::array<NameBuf, kMaxJoyButtons> raw{};
    std::array<NameBuf, kMaxJoyButtons> nnames{};
    std::array<NameBuf, kMaxJoyButtons> pnames{};
    std::size_t joy_count = 0;
    if (!j_list.empty()) {
        for (std::size_t n = 0; n < kMaxJoyButtons; ++n) {
            raw[n] = substr_field(j_list, n);
            if (raw[n][0] == '\0') break;
            nnames[n] = raw[n];
            for (std::size_t i = 0; i < kNameCap; ++i) {
                if (nnames[n][i] == '(') {
                    nnames[n][i] = '\0';
                    break;
                }
                if (nnames[n][i] == '\0') break;
            }
            trim_buf(nnames[n]);
            if (nnames[n][0] == '\0') break;
            ++joy_count;
        }
    }

    if (!jn_list.empty()) {
        for (NameBuf& b : nnames)
            b = NameBuf{};
        for (std::size_t n = 0; n < joy_count; ++n) {
            nnames[n] = substr_field(jn_list, n);
            trim_buf(nnames[n]);
        }
    }

    bool defaults = false;
    if (!jp_list.empty()) {
        defaults = gamepad_defaults;
        for (std::size_t n = 0; n < joy_count; ++n) {
            pnames[n] = substr_field(jp_list, n);
            trim_buf(pnames[n]);
        }
    }

    for (std::size_t i = 0, n = 0; i < joy_count; ++i) {
        if (buf_view(raw[i]) == "-") continue;

        NameBuf btn = defaults ? pnames[n] : nnames[n];

        for (std::size_t k = 0; k < kNameCap; ++k) {
            if (btn[k] == '|') {
                btn[k] = '\0';
                break;
            }
            if (btn[k] == '\0') break;
        }

        const std::int8_t sys = sys_for_button_name(buf_view(btn));
        const std::size_t idx = i + 4;
        if (sys >= JoyPlan::kFirstButtonWord && sys <= JoyPlan::kLastButtonWord &&
            idx < JoyPlan::kWords) {
            plan.src[idx] = sys;
        }

        const NameBuf& full = defaults ? pnames[n] : nnames[n];
        const std::string_view fv = buf_view(full);
        const std::size_t bar = fv.find('|');
        if (plan.paddle_idx == JoyPlan::kNone && bar != std::string_view::npos &&
            ieq(fv.substr(bar), "|P")) {
            plan.paddle_idx = static_cast<std::int8_t>(idx);
        }
        ++n;
    }

    plan.present = (joy_count != 0);
    return plan;
}

}  // namespace mister::svc
