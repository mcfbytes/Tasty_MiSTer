// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/x86/x86_boot_loader.h"

#include <string>

#include "cores/boot_asset.h"
#include "cores/window_spot.h"
#include "infra/prelude.h"
#include "svc/search_policy.h"
#include "svc/vfs.h"

namespace mister::cores::x86 {

Ex<LoadPlan> X86BootLoader::plan(const LoadAsk& ask, const proto::StatusWord&) {
    TASTY_SEAT_BODY(X86BootLoader);
    LoadPlan p{};
    if (!ask.disc) {
        p.count = 1;
        p.rows[0] = TransferRow{.index = ask.index};
        return p;
    }
    for (std::size_t i = 0; i < kShadow.size(); ++i) {
        const UmaRow& u = kShadow[i];
        TransferRow r{
            .dest = RowDest::Window,
            .index = ask.index,
            .place = WindowSpot{.slot = kGuestMemory, .offset = u.guest_at},
            .bracket = RowBracket::None,
            .fill = u.fill
        };
        if (u.rom.empty()) {
            r.extent = u.bytes;
        } else {
            std::string rel{home_};
            rel += '/';
            rel.append(u.rom);
            auto found = vfs_.resolve(rel, svc::SearchPolicy{kCoreHomeDir, true});
            if (!found)
                continue;
            paths_[i] = std::move(*found);
            auto f = vfs_.open(paths_[i], svc::OpenMode::ReadWhole);
            const auto size = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
            if (!size || size->empty())
                continue;
            r.source = paths_[i];
            r.set_file_extent(*size, *size);
        }
        p.rows[p.count++] = r;
    }
    return p;
}

Ex<void> X86BootLoader::finish(infra::OptRef<ICoreWindow>) {
    TASTY_SEAT_BODY(X86BootLoader);
    return {};
}

}
