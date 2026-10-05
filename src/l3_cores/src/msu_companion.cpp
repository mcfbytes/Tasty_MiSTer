// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/msu_companion.h"

#include "cores/manifests/snes.h"
#include "cores/msu_data_loader.h"
#include "cores/msu_wire.h"

namespace mister::cores {

std::string_view MsuCompanion::stem_of(std::string_view path) noexcept {
    const std::size_t slash = path.rfind('/');
    const std::size_t dot = path.rfind('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
        return path;
    return path.substr(0, dot);
}

Ex<CompanionPlan> MsuCompanion::plan(const CompanionAsk& ask) {
    if (!stem_.assign(stem_of(ask.path)))
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    CompanionPlan p{};
    p.stem = stem_.view();
    p.opcode = msu::kCdSet;
    std::uint64_t size = 0;
    if (ask.mailbox_live && data_.assign(stem_.view()) && data_.append(".msu")) {
        if (auto f = vfs_->open(data_.view(), svc::OpenMode::Read)) {
            p.present = true;
            const auto sz = (*f)->size();
            size = sz ? sz->v : 0;
        }
    }
    TransferRow row{.dest = RowDest::Window,
                    .index = proto::IoIndex{msu::kDataIndex},
                    .addr = 0,
                    .window = 0,
                    .bracket = RowBracket::AroundWithLength,
                    .source = data_.view(),
                    .placed = true};
    const auto base = row_address(row, manifests::kSnesWindows, ask.aperture_base);
    if (size != 0 && size <= msu::kDataBound && base) {
        const auto n = static_cast<std::uint32_t>(size);
        row.file_len = n;
        row.extent = msu::data_extent(n);
        row.length = n;
        p.loads = true;
        p.before = msu::set_data_base(*base);
        p.row = row;
    }
    p.after = msu::set_enable(p.present);
    p.poll = p.present ? proto::MailboxPoll::On : proto::MailboxPoll::Off;
    return p;
}

std::unique_ptr<ILoader> MsuCompanion::loader() { return std::make_unique<MsuDataLoader>(); }

}  // namespace mister::cores
