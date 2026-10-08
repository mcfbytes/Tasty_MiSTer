// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ini_parse.h"

#include "app/config_apply.h"
#include "svc/config_parser.h"
#include "svc/vfs.h"

#include <string>
#include <vector>

namespace mister::app {
namespace {

[[nodiscard]] Ex<std::vector<std::byte>> read_all(const svc::Vfs& vfs, std::string_view path) {
    auto f = vfs.open(path, svc::OpenMode::ReadWhole);
    if (!f) return std::unexpected(f.error());
    auto sz = (*f)->size();
    if (!sz) return std::unexpected(sz.error());
    std::vector<std::byte> buf(static_cast<std::size_t>(sz->v));
    std::size_t off = 0;
    while (off < buf.size()) {
        auto n = (*f)->read_at(off, std::span<std::byte>(buf).subspan(off));
        if (!n) return std::unexpected(n.error());
        if (*n == 0) {
            return std::unexpected(
                Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(off)});
        }
        off += *n;
    }
    return buf;
}

}  // namespace

[[nodiscard]] Ex<svc::ConfigSnapshot> parse_ini_for_core(const svc::Vfs* vfs,
                                                         const svc::ConfigSnapshot& defaults,
                                                         std::string_view conf_str_name,
                                                         const MraFacts& facts) {
    svc::ConfigSnapshot next = defaults;
    if (vfs == nullptr) {
        apply_vga_mode_fixup(next);
        return next;
    }
    std::string text;
    if (auto bytes = read_all(*vfs, "MiSTer.ini")) {
        text.assign(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    }
    if (!text.empty()) {
        svc::config_parser::PassNames names{};
        names.core_name = effective_name(conf_str_name, facts);
        names.orig_core_name = conf_str_name;
        names.is_arcade = facts.is_arcade;
        names.arcade_vertical = facts.vertical;
        auto parsed = svc::config_parser::parse_two_pass(text, names);
        if (!parsed) return std::unexpected(parsed.error());
        next = std::move(*parsed);
    }
    apply_vga_mode_fixup(next);
    return next;
}

[[nodiscard]] proto::ConfSwitches conf_switches(const svc::ConfigSnapshot& cfg) noexcept {
    using B = proto::ConfSwitches::Bit;
    proto::ConfSwitches c;
    c.set(B::VgaScaler, cfg.vga_scaler != 0)
        .set(B::VgaSog, cfg.vga_sog != 0)
        .set(B::Csync, cfg.csync != 0)
        .set(B::Ypbpr, cfg.vga_mode_int == 1)
        .set(B::ForcedScandoubler, cfg.forced_scandoubler != 0)
        .set(B::Audio96k, cfg.hdmi_audio_96k != 0)
        .set(B::Dvi, cfg.dvi_mode == 1)
        .set(B::HdmiLimited1, (cfg.hdmi_limited & 1u) != 0)
        .set(B::HdmiLimited2, (cfg.hdmi_limited & 2u) != 0)
        .set(B::DirectVideo, svc::direct_video_resolved(cfg) != 0)
        .set(B::DirectVideo2, svc::direct_video_resolved(cfg) == 2);
    return c;
}

[[nodiscard]] proto::ConfSwitches default_conf_switches() noexcept {
    return conf_switches(svc::ConfigSnapshot{});
}

}  // namespace mister::app
