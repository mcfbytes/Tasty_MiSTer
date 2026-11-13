// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/ini_parse.h"

#include "app/config_apply.h"
#include "app/types.h"
#include "infra/byte_view.h"
#include "svc/config_parser.h"
#include "svc/ini_catalog.h"
#include "svc/vfs.h"

#include <array>
#include <span>
#include <string>

namespace mister::app {
namespace {

constexpr std::string_view kLoadKindSections[] = {"arcade", "arcade_vertical"};

[[nodiscard]] std::span<const std::string_view> load_kind_sections(const MraFacts& facts) noexcept {
    if (facts.is_arcade && facts.vertical)
        return std::span{kLoadKindSections};
    if (facts.is_arcade)
        return std::span{kLoadKindSections}.first(1);
    if (facts.vertical)
        return std::span{kLoadKindSections}.last(1);
    return {};
}

}

[[nodiscard]] Ex<std::vector<std::byte>> read_ini_whole(svc::IFile& file) {
    auto sz = file.size();
    if (!sz)
        return std::unexpected(sz.error());
    const proto::FileSize size = *sz;
    if (size > proto::FileSize{kIniCap})
        return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
    std::vector<std::byte> out;
    std::array<std::byte, 4096> chunk{};

    proto::FileSize got{};
    while (got < size) {

        auto n = file.read_at(got.file_word<std::size_t>(), chunk);
        if (!n)
            return std::unexpected(n.error());
        if (*n == 0)
            return std::unexpected(Error{Errc::short_read, ERR_SITE(), 0});
        const proto::FileSize answered{*n};
        if (got + answered > size)
            return std::unexpected(Error{Errc::io, ERR_SITE(), 0});
        out.insert(out.end(), chunk.data(), chunk.data() + *n);
        got += answered;
    }
    if (got != size)
        return std::unexpected(Error{Errc::short_read, ERR_SITE(), 0});
    return out;
}

[[nodiscard]] Ex<std::vector<std::byte>> read_ini_whole(
    const svc::Vfs& vfs,
    std::string_view name
) {
    auto f = vfs.open(name, svc::OpenMode::ReadWhole);
    if (!f)
        return std::unexpected(f.error());
    return read_ini_whole(**f);
}

[[nodiscard]] svc::ConfigSnapshot parse_ini_for_core(
    const svc::Vfs* vfs,
    const svc::ConfigSnapshot& defaults,
    std::string_view conf_str_name,
    const MraFacts& facts,
    const svc::IniCatalog& ini
) {
    svc::ConfigSnapshot next = defaults;
    if (vfs == nullptr) {
        apply_vga_mode_fixup(next);
        return next;
    }
    std::string text;
    if (auto bytes = read_ini_whole(*vfs, ini.active_name())) {
        text.assign(infra::as_text(std::span(*bytes)));
    }
    if (!text.empty()) {
        svc::config_parser::PassNames names{};
        names.core_name = effective_name(conf_str_name, facts);
        names.orig_core_name = conf_str_name;
        names.core_sections = load_kind_sections(facts);
        next = svc::config_parser::parse_two_pass(text, names);
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
        .set(B::Ypbpr, vga_mode_of(cfg.vga_mode_int) == VgaMode::YPbPr)
        .set(B::ForcedScandoubler, cfg.forced_scandoubler != 0)
        .set(B::Audio96k, cfg.hdmi_audio_96k != 0)
        .set(B::Dvi, cfg.dvi_mode == 1)
        .set(B::HdmiLimited1, (cfg.hdmi_limited & 1u) != 0)
        .set(B::HdmiLimited2, (cfg.hdmi_limited & 2u) != 0)
        .set(B::DirectVideo, svc::direct_video_resolved(cfg) != svc::DirectVideo::Off)
        .set(B::DirectVideo2, svc::direct_video_resolved(cfg) == svc::DirectVideo::Auto);
    return c;
}

[[nodiscard]] proto::ConfSwitches default_conf_switches() noexcept {
    return conf_switches(svc::ConfigSnapshot{});
}

IdleKnobs idle_knobs_of(const svc::ConfigSnapshot& cfg) noexcept {
    return IdleKnobs{
        .osd_timeout = std::chrono::seconds{cfg.osd_timeout},
        .video_off = std::chrono::seconds{cfg.video_off},
        .video_off_logo = cfg.video_off_logo != 0,
        .fb_terminal = cfg.fb_terminal != 0
    };
}

}
