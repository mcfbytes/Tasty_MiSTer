// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/addon_send.h"

#include <utility>

#include "cores/ram_image_recipe.h"
#include "infra/ascii.h"
#include "infra/path_parts.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::app {

std::optional<PathText> AddonSend::path_of(std::string_view pick, std::string_view ext) {
    std::string name;
    if (const std::optional<std::string_view> dir = infra::dirname(pick)) {
        name.assign(*dir);
        name.push_back('/');
    }
    if (!ext.empty() && ext.front() == '!') {
        name.append(ext.substr(1));
    } else {
        name.append(infra::stem(pick));
        name.push_back('.');
        name.append(ext);
    }
    PathText out{};
    if (!out.assign(name))
        return std::nullopt;
    return out;
}

AddonSend AddonSend::plan(std::string_view list, std::string_view pick, proto::WideIoIndex idx) {
    AddonSend s;
    for (std::size_t i = 0; !list.empty(); ++i) {
        const auto no = EntryNo::of(i);
        if (!no)
            break;
        const std::size_t comma = list.find(',');
        const std::string_view ext = list.substr(0, comma);
        if (ext.empty())
            break;
        if (const auto path = path_of(pick, ext)) {
            s.entries_.push_back(
                Entry{
                    .ext = std::string{ext},
                    .dest = AddonDest::of(*no, idx.fio_byte()),
                    .path = *path
                }
            );
        }
        if (comma == std::string_view::npos)
            break;
        list.remove_prefix(comma + 1);
    }
    return s;
}

bool AddonSend::take_image(std::vector<std::uint8_t> bytes) {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (!infra::ieq(entries_[i].ext, cores::kRamImageAddon))
            continue;
        image_ = std::move(bytes);
        image_at_ = EntryNo::of(i);
        return true;
    }
    return false;
}

bool AddonSend::next_is_image() const noexcept {
    const auto here = EntryNo::of(next_);
    return image_at_ && here && *image_at_ == *here;
}

bool AddonSend::open_(const svc::Vfs& vfs) {
    const Entry& e = entries_[next_];
    const cores::PayloadPieces::Frame frame{
        .dest = e.dest.wide(),
        .chunk = kStockFileTxChunk,
        .progress = true
    };
    if (next_is_image()) {
        auto p = cores::PayloadPieces::of_bytes(std::move(image_), {}, frame);
        if (!p)
            return false;
        pieces_.emplace(std::move(*p));
        return true;
    }
    auto f = vfs.open(e.path.view(), svc::OpenMode::ReadWhole);
    const auto sz = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
    if (!sz)
        return false;
    auto p = cores::PayloadPieces::of_file(std::move(*f), *sz, {}, frame);
    if (!p)
        return false;
    pieces_.emplace(std::move(*p));
    return true;
}

AddonSend::Pass AddonSend::step(cores::ILadderHost& host, const svc::Vfs& vfs) {
    TASTY_SEAT_BODY(AddonSend);
    while (next_ < entries_.size()) {
        const bool image_entry = next_is_image();
        if (!pieces_ && !open_(vfs)) {
            if (image_entry)
                image_failed_ = true;
            ++missing_;
            ++next_;
            continue;
        }
        auto pass = pieces_->step(host);
        if (pass && *pass == cores::PayloadPieces::Pass::Waiting)
            return Pass::Waiting;
        const bool done = pass && *pass == cores::PayloadPieces::Pass::Done;
        done ? ++sent_ : ++failed_;
        if (image_entry)
            (done ? image_sent_ : image_failed_) = true;
        pieces_.reset();
        ++next_;
    }
    return Pass::Done;
}

bool AddonSend::wants_pass(const LinkTxChannel<SeatTag::Main>& inbox) const noexcept {
    TASTY_SEAT_BODY(AddonSend);
    return inbox.ring().size() < cores::PayloadPieces::kAhead;
}

bool AddonSend::wants_pass(
    const LinkTxChannel<SeatTag::Main>& inbox,
    const FileTxLevelCell&
) const noexcept {
    TASTY_SEAT_BODY(AddonSend);
    return wants_pass(inbox);
}

Rung::Pass AddonSend::step(const RungEdges& edges, cores::ILadderHost& owner) {
    TASTY_SEAT_BODY(AddonSend);
    switch (step(owner, edges.vfs)) {
        case Pass::Waiting:
            return Rung::Pass::Waiting;
        case Pass::Done:
            return Rung::Pass::Done;
    }
    return Rung::Pass::Failed;
}

}
