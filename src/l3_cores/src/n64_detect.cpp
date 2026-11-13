// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/n64_detect.h"

#include <cstdio>

namespace mister::cores::n64 {

namespace {

[[nodiscard]] std::uint8_t hex_to_dec(char c) noexcept {
    if (c >= '0' && c <= '9')
        return static_cast<std::uint8_t>(c - '0');
    if (c >= 'A' && c <= 'F')
        return static_cast<std::uint8_t>(c - 'A' + 10);
    if (c >= 'a' && c <= 'f')
        return static_cast<std::uint8_t>(c - 'a' + 10);
    return 0;
}

[[nodiscard]] bool pad_is_pinned(PadType p) noexcept {
    return p == PadType::Snac || p == PadType::Keyboard || p == PadType::Mouse;
}

}

DetectOutcome DetectLadder::run(const RomFacts& facts, LoadFacts& slab) {
    detected_ = lookup_and_apply_(slab, DbKey::md5(facts.header_md5.view()));
    if (detected_.none())
        detected_ = lookup_and_apply_(slab, DbKey::md5(facts.file_md5.view()));
    if (detected_.none() && cart_id_lookupable(facts.cart_id.view())) {
        detected_ = lookup_and_apply_(slab, DbKey::cart_id(facts.cart_id.view()));
    }
    DetectOutcome out{};
    if (detected_.none()) {
        if (apply_homebrew_(facts, slab)) {
            out.homebrew = true;
            detected_ = DetectFlags{}.with(DetectFlag::Db);
        } else if (slab.options().auto_detect()) {
            apply_unknown_defaults_(slab);
        }
    }

    if (!detected_.has(DetectFlag::Region)) {
        const std::string_view id = facts.cart_id.view();
        const char region = id.size() > 3 ? id[3] : '\0';
        const auto d = detect_from_bootcode(region, facts.bootcode);
        apply_ipl3_(slab, d, detected_);
    }
    out.detected = detected_;
    return out;
}

DetectFlags DetectLadder::lookup_and_apply_(LoadFacts& slab, const DbKey& key) {
    const auto match = reader_.scan(key);
    if (!match)
        return {};
    match_ = match;
    if (match->tags)
        apply_tags_(slab, *match->tags);
    return detect_level(*match, slab.options().auto_detect());
}

void DetectLadder::apply_tags_(LoadFacts& slab, const DbTags& t) {
    if (!slab.options().auto_detect())
        return;
    if (t.system != SystemType::Unknown) {
        slab.set_system(t.system);
    }
    if (t.cic != Cic::Unknown) {
        slab.set_cic(t.cic);
    }
    for (const FeatureRow& row : kFeatureRows)
        slab.set_flag(row.field, t.features.has(row.tag));
    slab.set_cart_save_type(t.save_type);

    if (slab.options().auto_pak() && !pad_is_pinned(slab.options().controller(PadPort::P1))) {
        slab.set_controller(PadPort::P1, t.preferred_pad);
    }
}

bool DetectLadder::apply_homebrew_(const RomFacts& facts, LoadFacts& slab) {
    const std::string_view id = facts.cart_id.view();
    if (id.size() < kCartIdChars)
        return false;
    if (id[1] != 'E' || id[2] != 'D')
        return false;

    if (!slab.options().auto_detect())
        return false;

    switch (hex_to_dec(id[4])) {
        case 1:
            slab.set_cart_save_type(MemoryType::Eeprom512);
            break;
        case 2:
            slab.set_cart_save_type(MemoryType::Eeprom2k);
            break;
        case 3:
            slab.set_cart_save_type(MemoryType::Sram32k);
            break;
        case 4:
            slab.set_cart_save_type(MemoryType::Sram96k);
            break;
        case 5:
            slab.set_cart_save_type(MemoryType::Flash128k);
            break;
        default:
            slab.set_cart_save_type(MemoryType::None);
            break;
    }

    FeatureSet features;
    if ((hex_to_dec(id[5]) & 1u) != 0u)
        features.add(Tag::Rtc);

    const auto& cs = facts.controllers;
    if (cs[0] == 0x01 || cs[1] == 0x01 || cs[2] == 0x01 || cs[3] == 0x01)
        features.add(Tag::Rpak);
    if (cs[0] == 0x02 || cs[1] == 0x02 || cs[2] == 0x02 || cs[3] == 0x02)
        features.add(Tag::Cpak);

    if (cs[0] == 0x03)
        features.add(Tag::Tpak);

    for (const FeatureRow& row : kFeatureRows)
        if (row.from_header)
            slab.set_flag(row.field, features.has(row.tag));

    if (!slab.options().auto_pak())
        return true;

    for (const PadPort port : kPadPorts) {
        const std::uint8_t want = cs[port_index(port)];
        if (want == 0)
            continue;
        if (pad_is_pinned(slab.options().controller(port)))
            continue;
        if (want < 0x80) {
            const PadType p = want == 0x01              ? PadType::PadWithRpak
                : want == 0x02                          ? PadType::PadWithCpak
                : (want == 0x03 && port == PadPort::P1) ? PadType::PadWithTpak
                                                        : PadType::Pad;
            slab.set_controller(port, p);
        } else if (want == 0xff) {
            slab.set_controller(port, PadType::Unplugged);
        }
    }
    return true;
}

void DetectLadder::apply_unknown_defaults_(LoadFacts& slab) {

    for (const FeatureRow& row : kFeatureRows)
        slab.set_flag(row.field, false);
    slab.set_cart_save_type(MemoryType::None);
}

void DetectLadder::apply_ipl3_(LoadFacts& slab, const Ipl3Detect& d, DetectFlags& detected) {
    if (!slab.options().auto_detect()) {

        detected = detected.with(DetectFlag::Region);
        return;
    }
    slab.set_system(d.system);
    slab.set_cic(d.cic);
    if (d.known_signature)
        detected = detected.with(DetectFlag::Region);
}

void DetectLadder::compose_info(const RomFacts& facts, const LoadFacts& slab, bool patched) {
    if (!slab.options().auto_detect())
        return;
    info_.clear();

    const auto app = [&](std::string_view s) {
        (void)info_.append(s);
    };

    app("Auto-detect:");

    const std::string_view id = facts.cart_id.view();
    if (!id.empty() && (id[1] != 'E' || id[2] != 'D')) {
        char line[40]{};
        const int n = std::snprintf(
            line,
            sizeof line,
            "\n[%.4s] v.%u.%u",
            facts.cart_id.c_str(),
            hex_to_dec(id[4]) + 1u,
            hex_to_dec(id[5])
        );
        if (n > 0)
            app(std::string_view(line, static_cast<std::size_t>(n)));
    }
    if (!facts.internal_name.empty()) {
        app("\n\"");
        app(facts.internal_name.view());
        app("\"");
    }

    if (!detected_.has(DetectFlag::Region)) {
        app("\nUnknown Region/CIC");
    } else {
        const SystemType sys = slab.options().system();
        const Cic cic = slab.options().cic();
        app("\nRegion: ");
        app(system_text(sys));
        app(" (");
        app(cic_text(cic));
        app(")");
    }

    if (!detected_.has(DetectFlag::Db)) {
        app("\nROM missing from database.\nYou might not be able to save.");
    } else {
        const MemoryType save = slab.options().cart_save_type();
        if (save != MemoryType::None) {
            app("\nSave Type: ");
            app(save_type_text(save));
        }
        for (const FeatureRow& row : kFeatureRows)
            if (slab.options().flag(row.field))
                app(row.info);
        if (patched)
            app("\nPatched \x96");
    }
}

}
