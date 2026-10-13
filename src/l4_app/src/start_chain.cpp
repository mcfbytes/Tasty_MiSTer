// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/start_chain.h"

#include <cstddef>
#include <string>

#include "app/bitstream_programmer.h"
#include "app/stream_io.h"
#include "svc/search_policy.h"
#include "svc/vfs.h"

namespace mister::app {

StartChain StartChain::plan(const Facts& f, const svc::Vfs& vfs) {
    StartChain c;
    switch (f.rule) {
        case cores::StartRule::None:
            break;
        case cores::StartRule::Assets:
            c.plan_assets_(f, vfs);
            break;
        case cores::StartRule::Chain:
            c.plan_chain_(f, vfs);
            break;
    }
    return c;
}

void StartChain::plan_assets_(const Facts& f, const svc::Vfs& vfs) {
    if (f.ladder) return;
    for (const cores::BootAsset& row : f.declared) {
        if (!cores::start_row_sends(row, f.index0_taken)) continue;
        std::string rel(row.subdir);
        if (!rel.empty()) rel += '/';
        rel.append(row.name);
        const auto path = vfs.resolve(rel, svc::SearchPolicy{row.where, true});
        if (path) add_(*path, row.dest);
    }
}

void StartChain::plan_chain_(const Facts& f, const svc::Vfs& vfs) {
    if (!f.generic_identity || f.manifest_load) return;
    const std::string home = core_home(f.home, vfs);
    if (f.rbf.empty() || !plan_composite_(f.rbf, vfs)) {
        for (const cores::BootAsset& row : cores::kCoreHomeChain) {
            if (home.empty() || !cores::start_row_sends(row, f.index0_taken)) continue;
            std::string path = home;
            path += '/';
            path.append(row.name);
            if (vfs.file_exists(path)) add_(path, row.dest);
        }
    }
    hold_ = count_ > 0 || f.remembered;
    plan_boot_images_(f.slot0_held, home, vfs);
}

void StartChain::plan_boot_images_(bool slot0_held, std::string_view home, const svc::Vfs& vfs) {
    if (home.empty()) return;
    const auto image = [&](std::string_view name, unsigned slot) {
        std::string path(home);
        path += '/';
        path.append(name);
        if (!vfs.file_exists(path) || image_count_ >= images_.size()) return;
        if (auto s = file_(path, proto::WideIoIndex{static_cast<std::uint16_t>(slot << 6)}, 0))
            images_[image_count_++] = *s;
    };
    for (unsigned i = slot0_held ? 1u : 0u; i < 4; ++i) {
        std::string name = "boot";
        name += static_cast<char>('0' + i);
        name += ".vhd";
        image(name, i);
    }
    if (!slot0_held) image("boot.vhd", 0);
}

std::string StartChain::core_home(std::string_view name, const svc::Vfs& vfs) {
    if (name.empty()) return {};
    std::string games = "games/";
    games.append(name);
    for (const svc::SearchDir& root : svc::search::kPrefixed) {
        const svc::SearchPolicy one{std::span<const svc::SearchDir>(&root, 1), true};
        for (const std::string_view rel : {name, std::string_view(games)}) {
            if (auto p = vfs.resolve(rel, one); p && vfs.dir_exists(*p)) return *p;
        }
    }
    return {};
}

bool StartChain::plan_composite_(std::string_view rbf, const svc::Vfs& vfs) {
    auto src = stream_open(vfs, rbf, svc::OpenMode::Read);
    if (!src || src->size < kContainerHeaderBytes) return false;
    std::byte hdr[kContainerHeaderBytes];
    if (!stream_read(*src->f, 0, std::span<std::byte>(hdr, sizeof(hdr)))) return false;
    const auto w = container_window(std::span<const std::byte>(hdr, sizeof(hdr)), src->size);
    if (!w || w->base != kContainerHeaderBytes) return false;
    const std::uint64_t tail = w->base + w->length;
    if (tail < src->size) add_(rbf, proto::WideIoIndex{0}, tail);
    return true;
}

void StartChain::add_(std::string_view path, proto::WideIoIndex dest, std::uint64_t offset) {
    if (count_ >= files_.size()) return;
    if (auto s = file_(path, dest, offset)) files_[count_++] = *s;
}

std::optional<StartFile> StartChain::file_(std::string_view path, proto::WideIoIndex dest,
                                           std::uint64_t offset) {
    StartFile s{};
    if (!s.path.assign(path)) return std::nullopt;
    s.dest = dest;
    s.offset = offset;
    return s;
}

}  // namespace mister::app
