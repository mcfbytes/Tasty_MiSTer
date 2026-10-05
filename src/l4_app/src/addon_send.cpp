// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/addon_send.h"

#include <cctype>
#include <utility>

#include "cores/ram_image.h"
#include "svc/file.h"
#include "svc/vfs.h"

namespace mister::app {

namespace {

bool iequal(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::toupper(static_cast<unsigned char>(a[i])) !=
            std::toupper(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

constexpr std::size_t kMaxEntries = 255;

}  // namespace

std::optional<PathText> AddonSend::path_of(std::string_view pick, std::string_view ext) {
    const std::size_t dot = pick.rfind('.');
    const std::string_view stem = dot == std::string_view::npos ? pick : pick.substr(0, dot);
    std::string name;
    if (!ext.empty() && ext.front() == '!') {
        const std::size_t slash = stem.rfind('/');
        if (slash != std::string_view::npos) name.assign(stem.substr(0, slash + 1));
        name.append(ext.substr(1));
    } else {
        name.assign(stem);
        name.push_back('.');
        name.append(ext);
    }
    PathText out{};
    if (!out.assign(name)) return std::nullopt;
    return out;
}

AddonSend AddonSend::plan(std::string_view list, std::string_view pick, std::uint8_t idx) {
    AddonSend s;
    for (std::size_t i = 1; !list.empty() && i <= kMaxEntries; ++i) {
        const std::size_t comma = list.find(',');
        const std::string_view ext = list.substr(0, comma);
        if (ext.empty()) break;
        if (const auto path = path_of(pick, ext)) {
            s.entries_.push_back(
                Entry{.ext = std::string{ext},
                      .dest = proto::WideIoIndex{static_cast<std::uint16_t>((i << 8) | idx)},
                      .path = *path});
        }
        if (comma == std::string_view::npos) break;
        list.remove_prefix(comma + 1);
    }
    return s;
}

bool AddonSend::take_image(std::vector<std::uint8_t> bytes) {
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (!iequal(entries_[i].ext, cores::kRamImageAddon)) continue;
        image_ = std::move(bytes);
        image_at_ = i;
        return true;
    }
    return false;
}

bool AddonSend::open_(const svc::Vfs& vfs) {
    const Entry& e = entries_[next_];
    const cores::PayloadPieces::Frame frame{.dest = e.dest, .chunk = kChunk, .progress = true};
    if (next_ == image_at_) {
        auto p = cores::PayloadPieces::of_bytes(std::move(image_), {}, frame);
        if (!p) return false;
        pieces_.emplace(std::move(*p));
        return true;
    }
    auto f = vfs.open(e.path.view(), svc::OpenMode::ReadWhole);
    const auto sz = f ? (*f)->size() : Ex<svc::FileSize>{std::unexpected(f.error())};
    if (!sz) return false;
    auto p = cores::PayloadPieces::of_file(std::move(*f), sz->v, {}, frame);
    if (!p) return false;
    pieces_.emplace(std::move(*p));
    return true;
}

AddonSend::Pass AddonSend::step(cores::ILadderHost& host, const svc::Vfs& vfs) {
    while (next_ < entries_.size()) {
        if (!pieces_ && !open_(vfs)) {
            if (next_ == image_at_) image_failed_ = true;
            ++missing_;
            ++next_;
            continue;
        }
        const cores::PayloadPieces::Pass pass = pieces_->step(host);
        if (pass == cores::PayloadPieces::Pass::Waiting) return Pass::Waiting;
        const bool done = pass == cores::PayloadPieces::Pass::Done;
        done ? ++sent_ : ++failed_;
        if (next_ == image_at_) (done ? image_sent_ : image_failed_) = true;
        pieces_.reset();
        ++next_;
    }
    return Pass::Done;
}

bool AddonSend::wants_pass(const LinkTxChannel& inbox) const noexcept {
    return inbox.ring().size() < cores::PayloadPieces::kAhead;
}

}  // namespace mister::app
