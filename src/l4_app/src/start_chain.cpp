// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/start_chain.h"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "app/bitstream_programmer.h"
#include "app/stream_io.h"
#include "cores/asset_sweep.h"
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
    if (f.ladder)
        return;
    for (const cores::BootAsset& row : f.declared) {
        if (!cores::start_row_sends(row, f.index0_taken))
            continue;

        if (row.unless
            && cores::host_value(f.status, f.host_bits, *row.unless) != proto::FieldValue{})
            continue;
        std::string rel(row.subdir);
        if (!rel.empty())
            rel += '/';
        rel.append(row.name);
        const auto path = vfs.resolve(rel, svc::SearchPolicy{row.where, true});
        if (path)
            add_(*path, row.dest);
    }
}

void StartChain::plan_chain_(const Facts& f, const svc::Vfs& vfs) {
    if (!f.generic_identity || f.manifest_load)
        return;
    const std::string home = core_home(f.home, vfs);
    const std::span<const cores::BootAsset> rows = f.declared.empty()
        ? std::span<const cores::BootAsset>(cores::kCoreHomeChain)
        : f.declared;
    const bool composite = !f.rbf.empty() && plan_composite_(f.rbf, vfs);

    const auto add_present = [&](std::string_view name, proto::WideIoIndex dest) {
        std::string path(home);
        path += '/';
        path.append(name);
        if (vfs.file_exists(path))
            add_(path, dest);
    };
    for (const cores::BootAsset& row : rows) {
        if (home.empty())
            break;
        switch (row.sweep) {
            case cores::AssetSweep::None:
                if (!composite && sends_(row, f))
                    add_present(row.name, row.dest);
                break;
            case cores::AssetSweep::HexPage:
                for (cores::HexPagePlace page{}; page != cores::HexPagePlace::end();
                     page = page.next()) {
                    const auto suffix = page.suffix();
                    std::string name(row.name);
                    name.append(suffix.data(), suffix.size());
                    add_present(name, row.dest);
                }
                break;
        }
    }
    hold_ = !files_.empty() || f.remembered;
    plan_boot_images_(f.slot0_held, home, vfs);
}

bool StartChain::sends_(const cores::BootAsset& row, const Facts& f) noexcept {
    if (!cores::start_row_sends(row, f.index0_taken))
        return false;
    return std::ranges::none_of(f.remembered_exts, [&row](std::string_view ext) {
        return row.dropped_by(ext);
    });
}

void StartChain::plan_boot_images_(bool slot0_held, std::string_view home, const svc::Vfs& vfs) {
    if (home.empty())
        return;
    const auto image = [&](std::string_view name, proto::WideIoIndex dest) {
        std::string path(home);
        path += '/';
        path.append(name);
        if (!vfs.file_exists(path) || image_count_ >= images_.size())
            return;
        if (auto s = file_(path, dest, proto::kFileStart))
            images_[image_count_++] = *s;
    };

    for (std::uint8_t i = slot0_held ? std::uint8_t{1} : std::uint8_t{0}; i < kNumberedBootImages;
         ++i) {
        std::string name = "boot";
        name += static_cast<char>('0' + i);
        name += ".vhd";
        image(name, proto::WideIoIndex::of(proto::IoIndex{}, i));
    }
    if (!slot0_held)
        image("boot.vhd", proto::WideIoIndex{});
}

std::string StartChain::core_home(std::string_view name, const svc::Vfs& vfs) {
    if (name.empty())
        return {};
    std::string games = "games/";
    games.append(name);
    for (const svc::SearchDir& root : svc::search::kPrefixed) {
        const svc::SearchPolicy one{std::span<const svc::SearchDir>(&root, 1), true};
        for (const std::string_view rel : {name, std::string_view(games)}) {
            if (auto p = vfs.resolve(rel, one); p && vfs.dir_exists(*p))
                return *p;
        }
    }
    return {};
}

bool StartChain::plan_composite_(std::string_view rbf, const svc::Vfs& vfs) {
    auto src = stream_open(vfs, rbf, svc::OpenMode::Read);
    if (!src)
        return false;
    const auto w = read_payload_window(*src->f, src->size);
    if (!w || w->base != kPayloadAt)
        return false;
    const proto::FileOffset tail = w->base + w->length;
    if (src->size.holds(tail))
        add_(rbf, proto::WideIoIndex{0}, tail);
    return true;
}

void StartChain::add_(std::string_view path, proto::WideIoIndex dest, proto::FileOffset at) {
    if (auto s = file_(path, dest, at))
        files_.push_back(*s);
}

std::optional<StartFile> StartChain::file_(
    std::string_view path,
    proto::WideIoIndex dest,
    proto::FileOffset at
) {
    StartFile s{};
    if (!s.path.assign(path))
        return std::nullopt;
    s.dest = dest;
    s.at = at;
    return s;
}

}
