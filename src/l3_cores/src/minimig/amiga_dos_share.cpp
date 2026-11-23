// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/minimig/amiga_dos_share.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <utility>

#include "cores/minimig/share_packet.h"
#include "cores/minimig/share_word.h"
#include "infra/ascii.h"
#include "infra/span_cut.h"
#include "svc/move_names.h"

namespace mister::cores::minimig {

namespace {

using Kind = HostFileSandbox::Kind;

constexpr ShareReply::Volume kNoVolume{
    .total = proto::FileSize::of(ShareWord::kDosBlock * std::size_t{10}),
    .used = proto::FileSize::of(ShareWord::kDosBlock)
};

[[nodiscard]] std::string text_of(
    std::span<const std::byte> bytes,
    infra::ByteCount most = infra::ByteCount{std::numeric_limits<std::size_t>::max()}
) {
    std::string out;
    for (const std::byte b : most.first_in(bytes)) {
        const char c = std::to_integer<char>(b);
        if (c == '\0')
            break;
        out.push_back(c);
    }
    return out;
}

[[nodiscard]] std::string_view guest(const std::string& path) noexcept {
    return path.empty() ? std::string_view{"."} : std::string_view{path};
}

[[nodiscard]] std::string joined(const std::string& dir, std::string_view name) {
    std::string out = dir;
    if (!out.empty())
        out.push_back('/');
    out.append(name);
    return out;
}

[[nodiscard]] bool names_the_root(std::string_view root) noexcept {
    return root.empty()
        || infra::istarts_with(kShareDevice, root)
        || infra::istarts_with(kShareVolume, root);
}

[[nodiscard]] ShareReply::Count counted(infra::ByteCount bytes) noexcept {
    return ShareReply::Count{mailbox_len(bytes)};
}

[[nodiscard]] std::optional<ShareWord> body_word(const ShareReply::Body& body) noexcept {
    if (const auto* l = std::get_if<ShareReply::LockAnswer>(&body))
        return ShareWord::of(l->key);
    if (const auto* f = std::get_if<ShareReply::FileAnswer>(&body))
        return ShareWord::of(f->key);
    if (const auto* c = std::get_if<ShareReply::Count>(&body))
        return ShareWord::of(c->bytes);
    if (const auto* p = std::get_if<ShareReply::Position>(&body))
        return ShareWord::of(p->at);
    return std::nullopt;
}

[[nodiscard]] ShareReply failed(const ShareRequest& asked, ShareError error) noexcept {
    return ShareReply{.action = asked.action, .error = error};
}

[[nodiscard]] ShareReply answered(const ShareRequest& asked, ShareReply::Body body) noexcept {
    return ShareReply{.action = asked.action, .body = std::move(body)};
}

[[nodiscard]] infra::ByteCount reply_bytes(const ShareReply& r) noexcept {
    switch (r.action) {
        case ShareAction::LocateObject:
        case ShareAction::CopyLock:
        case ShareAction::Parent:
        case ShareAction::FindInput:
        case ShareAction::FindOutput:
        case ShareAction::FindUpdate:
        case ShareAction::Read:
        case ShareAction::Write:
        case ShareAction::Seek:
        case ShareAction::CreateDir:
            return kWordBytes;
        case ShareAction::ExamineObject:
        case ShareAction::ExamineNext:
        case ShareAction::ExamineFh:
            if (const auto* e = std::get_if<ShareReply::Examined>(&r.body))
                return kExamineBytes + infra::ByteCount{e->name.size()};
            return kExamineBytes;
        case ShareAction::DiskInfo:
        case ShareAction::Info:
            return kVolumeBytes;
        case ShareAction::Nil:
        case ShareAction::FreeLock:
        case ShareAction::DeleteObject:
        case ShareAction::RenameObject:
        case ShareAction::SetProtect:
        case ShareAction::SetComment:
        case ShareAction::SameLock:
        case ShareAction::End:
            break;
    }
    return kGenericBytes;
}

}

Ex<ShareRequest> AmigaDosShare::decode(std::span<const std::byte> packet) const {
    TASTY_SEAT_BODY(AmigaDosShare);
    if (infra::ByteCount{packet.size()} < kHeaderBytes) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    ShareRequest r;
    r.action = ShareWord::load(packet, kTypeAt).action();

    std::vector<std::byte> own(packet.begin(), packet.end());
    if (const std::optional<infra::ByteOffset> term = ShareWord::load(packet, kSizeAt)
                                                          .terminator_in(packet)) {
        r.terminator = *term;
        infra::affine::from_clamped(std::span{own}, *term)[0] = std::byte{0};
    }
    const std::span<const std::byte> p{own};
    switch (r.action) {
        case ShareAction::LocateObject:
            r.lock = ShareWord::load(p, kKeyAt).lock_key();
            r.name = text_of(infra::affine::from_clamped(p, kLocateNameAt));
            break;
        case ShareAction::FindInput:
        case ShareAction::FindOutput:
        case ShareAction::FindUpdate:
        case ShareAction::DeleteObject:
        case ShareAction::CreateDir:
            r.lock = ShareWord::load(p, kKeyAt).lock_key();
            r.name = text_of(infra::affine::from_clamped(p, kObjectNameAt));
            break;
        case ShareAction::RenameObject: {
            r.lock = ShareWord::load(p, kKeyAt).lock_key();
            r.other = ShareWord::load(p, kSecondAt).lock_key();
            const std::span<const std::byte> length_at = infra::affine::from_clamped(
                p,
                kRenameLengthAt
            );
            const infra::ByteCount first{
                length_at.empty() ? 0 : std::to_integer<std::size_t>(length_at[0])
            };
            r.name = text_of(infra::affine::from_clamped(p, kRenameNameAt), first);
            r.new_name = text_of(infra::affine::from_clamped(p, kRenameNameAt + first));
            break;
        }
        case ShareAction::SameLock:
            r.lock = ShareWord::load(p, kKeyAt).lock_key();
            r.other = ShareWord::load(p, kSecondAt).lock_key();
            break;
        case ShareAction::ExamineObject:
        case ShareAction::ExamineNext:
            r.lock = ShareWord::load(p, kKeyAt).lock_key();
            r.cursor = ShareWord::load(p, kSecondAt).cursor();
            break;
        case ShareAction::ExamineFh:
        case ShareAction::End:
            r.file = ShareWord::load(p, kKeyAt).file_key();
            break;
        case ShareAction::Read:
        case ShareAction::Write:
            r.file = ShareWord::load(p, kKeyAt).file_key();
            r.length = ShareWord::load(p, kThirdAt).length();
            break;
        case ShareAction::Seek:
            r.file = ShareWord::load(p, kKeyAt).file_key();
            r.seek = ShareWord::load(p, kSecondAt).seek_to();
            r.seek_from = ShareWord::load(p, kThirdAt).seek_from();
            break;
        case ShareAction::Nil:
        case ShareAction::FreeLock:
        case ShareAction::CopyLock:
        case ShareAction::Parent:
        case ShareAction::SetProtect:
        case ShareAction::SetComment:
        case ShareAction::DiskInfo:
        case ShareAction::Info:
            r.lock = ShareWord::load(p, kKeyAt).lock_key();
            break;
    }
    return r;
}

void AmigaDosShare::reset() noexcept {
    TASTY_SEAT_BODY(AmigaDosShare);
    locks_.clear();
    files_.clear();
    next_lock_ = ShareLockKey{1};
    next_file_ = ShareFileKey{1};
}

ShareLockKey AmigaDosShare::add_lock_(std::optional<SharePath> path) {
    ShareLockKey key = next_lock_;
    while (locks_.contains(key))
        key = key.next();
    next_lock_ = key.next();
    locks_[key] = Lock{.path = std::move(path)};
    return key;
}

ShareFileKey AmigaDosShare::take_file_key_() {
    ShareFileKey key = next_file_;
    while (files_.contains(key))
        key = key.next();
    next_file_ = key.next();
    return key;
}

bool AmigaDosShare::has_locks_(const SharePath& path) const noexcept {
    return std::ranges::any_of(locks_, [&path](const auto& kv) { return kv.second.path == path; });
}

std::optional<AmigaDosShare::SharePath> AmigaDosShare::find_path_(
    const HostFileSandbox& share,
    ShareLockKey key,
    std::string_view name
) const {
    if (const std::size_t colon = name.find(':'); colon != std::string_view::npos) {
        if (names_the_root(name.substr(0, colon)))
            key = ShareLockKey{};
        name = name.substr(colon + 1);
    }

    std::string t;
    if (!key.is_root()) {
        if (const auto it = locks_.find(key); it != locks_.end()) {
            if (!it->second.path)
                return std::nullopt;
            if (!it->second.path->empty())
                t = "/" + *it->second.path;
        }
    }
    if (!name.empty()) {
        t.push_back('/');
        t.append(name);
    }
    for (std::size_t cur = 0; cur < t.size();) {
        ++cur;
        const std::size_t found = t.find('/', cur);
        const std::size_t next = found == std::string::npos ? t.size() : found;
        const std::string_view part{t.data() + cur, next - cur};
        if (part == "." || part == "..")
            return std::nullopt;
        if (next == t.size())
            break;
        if (!part.empty()) {
            cur = next;
            continue;
        }

        constexpr std::size_t kParentStep = 2;
        if (cur < kParentStep)
            return std::nullopt;
        std::size_t back = cur - kParentStep;
        while (t[back] != '/')
            --back;
        t.erase(back, next - back);
        cur = back;
    }
    if (!t.empty() && t.back() == '/')
        t.pop_back();
    SharePath path = t.empty() ? SharePath{} : t.substr(1);

    if (!path.empty()) {
        const std::size_t cut = path.rfind('/');
        const SharePath dir = cut == SharePath::npos ? SharePath{} : path.substr(0, cut);
        if (share.kind(guest(dir)) != Kind::Dir)
            return std::nullopt;
    }
    return path;
}

Ex<ShareReply> AmigaDosShare::serve(
    ShareRequest& asked,
    HostFileSandbox& share,
    MailboxWindow& window
) {
    TASTY_SEAT_BODY(AmigaDosShare);

    if (!share.ready())
        (void)share.make_root();
    ShareReply r = [&]() -> ShareReply {
        switch (asked.action) {
            case ShareAction::LocateObject:
                return locate_(asked, share);
            case ShareAction::FreeLock:
                locks_.erase(asked.lock);
                return answered(asked, ShareReply::Bare{});
            case ShareAction::CopyLock:
                return copy_lock_(asked);
            case ShareAction::Parent:
                return parent_(asked);
            case ShareAction::ExamineObject:
            case ShareAction::ExamineNext:
                return examine_(asked, share);
            case ShareAction::ExamineFh:
                return examine_fh_(asked, share);
            case ShareAction::FindInput:
            case ShareAction::FindOutput:
            case ShareAction::FindUpdate:
                return find_(asked, share);
            case ShareAction::Read:
                return read_(asked, window);
            case ShareAction::Write:
                return write_(asked, window);
            case ShareAction::Seek:
                return seek_(asked);
            case ShareAction::End:
                files_.erase(asked.file);
                return answered(asked, ShareReply::Bare{});
            case ShareAction::DeleteObject:
                return delete_(asked, share);
            case ShareAction::RenameObject:
                return rename_(asked, share);
            case ShareAction::CreateDir:
                return create_dir_(asked, share);
            case ShareAction::DiskInfo:
            case ShareAction::Info:
                return info_(asked, share);
            case ShareAction::SetProtect:
            case ShareAction::SetComment:
                return answered(asked, ShareReply::Bare{});
            case ShareAction::SameLock:
                return same_lock_(asked);
            case ShareAction::Nil:
                break;
        }
        return failed(asked, ShareError::ActionNotKnown);
    }();
    r.terminator = asked.terminator;
    return r;
}

ShareReply AmigaDosShare::locate_(const Request& asked, const HostFileSandbox& share) {
    const auto path = find_path_(share, asked.lock, asked.name);
    if (!path || share.kind(guest(*path)) == Kind::Absent) {
        return failed(asked, ShareError::ObjectNotFound);
    }
    return answered(asked, ShareReply::LockAnswer{add_lock_(*path)});
}

ShareReply AmigaDosShare::copy_lock_(const Request& asked) {
    const auto it = locks_.find(asked.lock);
    if (it == locks_.end())
        return failed(asked, ShareError::ObjectNotFound);
    const std::optional<SharePath> path = it->second.path;
    return answered(asked, ShareReply::LockAnswer{add_lock_(path)});
}

ShareReply AmigaDosShare::parent_(const Request& asked) {
    const auto it = locks_.find(asked.lock);
    if (it == locks_.end())
        return failed(asked, ShareError::ObjectNotFound);
    if (!it->second.path || it->second.path->empty()) {
        return answered(asked, ShareReply::LockAnswer{});
    }
    const SharePath& path = *it->second.path;
    const std::size_t cut = path.rfind('/');
    SharePath up = cut == SharePath::npos ? SharePath{} : path.substr(0, cut);
    return answered(asked, ShareReply::LockAnswer{add_lock_(std::move(up))});
}

ShareReply AmigaDosShare::examine_(const Request& asked, const HostFileSandbox& share) {
    const auto held = locks_.find(asked.lock);
    SharePath target{};
    if (held != locks_.end() && held->second.path)
        target = *held->second.path;

    Lock& lock = locks_[asked.lock];
    ShareReply::Examined e;
    SharePath object = target;
    if (asked.action == ShareAction::ExamineObject) {
        const std::size_t cut = target.rfind('/');
        e.name = target.empty()      ? std::string{kShareRootName}
            : cut == SharePath::npos ? target
                                     : target.substr(cut + 1);
        lock.listing.clear();
        if (share.kind(guest(target)) == Kind::Dir) {
            auto names = share.list(guest(target));
            if (!names)
                return failed(asked, ShareError::ObjectWrongType);
            for (svc::DirEntry& d : *names)
                lock.listing.push_back(std::move(d.name));
        }
    } else {
        const std::string* listed = asked.cursor.in(lock.listing);
        if (listed == nullptr) {
            lock.listing.clear();
            return failed(asked, ShareError::NoMoreEntries);
        }
        e.cursor = asked.cursor.next();
        e.name = *listed;
        object = joined(target, e.name);
    }
    const Kind kind = share.kind(guest(object));
    if (kind == Kind::Absent)
        return failed(asked, ShareError::ObjectNotFound);
    e.entry = kind == Kind::File ? ShareEntry::File : ShareEntry::UserDir;
    std::chrono::sys_seconds when{};
    if (const auto m = share.modified(guest(object)))
        when = *m;
    if (kind == Kind::File) {
        if (auto f = share.open(guest(object)); f) {
            if (const auto size = (*f)->size()) {
                e.size = *size;
            }
        }
    }
    e.date = AmigaDate::of(when);
    return answered(asked, std::move(e));
}

ShareReply AmigaDosShare::examine_fh_(const Request& asked, const HostFileSandbox& share) const {
    const auto it = files_.find(asked.file);
    if (it == files_.end())
        return failed(asked, ShareError::ObjectNotFound);
    const OpenFile& open = it->second;
    const auto size = open.file->size();
    const auto when = share.modified(guest(open.path));
    if (!size || !when)
        return failed(asked, ShareError::ObjectNotFound);
    ShareReply::Examined e;
    const std::size_t cut = open.path.rfind('/');
    e.name = cut == SharePath::npos ? open.path : open.path.substr(cut + 1);
    e.entry = ShareEntry::File;
    e.size = *size;
    e.date = AmigaDate::of(*when);
    return answered(asked, std::move(e));
}

ShareReply AmigaDosShare::find_(const Request& asked, HostFileSandbox& share) {
    const auto path = find_path_(share, asked.lock, asked.name);
    if (!path)
        return failed(asked, ShareError::ObjectNotFound);
    if (share.kind(guest(*path)) == Kind::Dir)
        return failed(asked, ShareError::ObjectWrongType);
    const ShareFileKey key = take_file_key_();
    const svc::OpenMode mode = asked.action == ShareAction::FindOutput ? svc::OpenMode::Truncate
        : asked.action == ShareAction::FindUpdate                      ? svc::OpenMode::Create
                                                                       : svc::OpenMode::ReadWrite;
    auto file = share.open(guest(*path), mode);
    if (!file)
        return failed(asked, ShareError::ObjectNotFound);
    files_[key] = OpenFile{.file = std::move(*file), .path = *path};
    return answered(asked, ShareReply::FileAnswer{key});
}

ShareReply AmigaDosShare::read_(const Request& asked, const MailboxWindow& window) {
    const auto it = files_.find(asked.file);
    if (it == files_.end())
        return failed(asked, ShareError::ObjectNotFound);
    OpenFile& open = it->second;
    const infra::ByteCount want = std::min(asked.length, infra::ByteCount{window.size()});
    const auto dst = window.at(MailboxSpan{.off = {}, .len = mailbox_len(want)});
    if (!dst)
        return failed(asked, ShareError::ObjectNotFound);
    infra::ByteCount got{};
    while (got < infra::ByteCount{dst->size()}) {
        const auto n = open.file->read_at(open.at + proto::FileSize::of(got), got.past_in(*dst));
        if (!n)
            return answered(asked, ShareReply::Count{});
        if (n->empty())
            break;
        got += *n;
    }
    open.at += proto::FileSize::of(got);
    return answered(asked, counted(got));
}

ShareReply AmigaDosShare::write_(const Request& asked, const MailboxWindow& window) {
    const auto it = files_.find(asked.file);
    if (it == files_.end())
        return failed(asked, ShareError::ObjectNotFound);
    OpenFile& open = it->second;
    const infra::ByteCount want = std::min(asked.length, infra::ByteCount{window.size()});
    const auto src = window.at(MailboxSpan{.off = {}, .len = mailbox_len(want)});
    if (!src)
        return failed(asked, ShareError::ObjectNotFound);
    if (!open.file->write_all_at(open.at, *src))
        return answered(asked, ShareReply::Count{});
    open.at += proto::FileSize::of(*src);
    return answered(asked, counted(want));
}

ShareReply AmigaDosShare::seek_(const Request& asked) {
    const auto it = files_.find(asked.file);
    if (it == files_.end())
        return failed(asked, ShareError::ObjectNotFound);
    OpenFile& open = it->second;
    const proto::FileOffset old = open.at;
    proto::FileOffset base = proto::kFileStart;
    if (asked.seek_from == ShareSeek::Current)
        base = old;
    if (asked.seek_from == ShareSeek::End) {
        const auto size = open.file->size();
        if (!size)
            return failed(asked, ShareError::SeekError);
        base = proto::kFileStart + *size;
    }
    const auto to = asked.seek.past(base);
    if (!to)
        return failed(asked, ShareError::SeekError);
    open.at = *to;
    return answered(asked, ShareReply::Position{old});
}

ShareReply AmigaDosShare::delete_(const Request& asked, HostFileSandbox& share) const {
    const auto path = find_path_(share, asked.lock, asked.name);
    if (!path)
        return failed(asked, ShareError::ObjectNotFound);
    if (has_locks_(*path))
        return failed(asked, ShareError::ObjectInUse);
    switch (share.kind(guest(*path))) {
        case Kind::Dir:
            return share.remove_dir(guest(*path)) ? answered(asked, ShareReply::Bare{})
                                                  : failed(asked, ShareError::DirectoryNotEmpty);
        case Kind::File:
            return share.remove(guest(*path)) ? answered(asked, ShareReply::Bare{})
                                              : failed(asked, ShareError::ObjectNotFound);
        case Kind::Absent:
            break;
    }
    return failed(asked, ShareError::ObjectNotFound);
}

ShareReply AmigaDosShare::rename_(const Request& asked, HostFileSandbox& share) const {
    const auto from = find_path_(share, asked.lock, asked.name);
    if (!from || share.kind(guest(*from)) == Kind::Absent) {
        return failed(asked, ShareError::ObjectNotFound);
    }
    const auto to = find_path_(share, asked.other, asked.new_name);
    if (!to)
        return failed(asked, ShareError::ObjectNotFound);
    if (*from == *to)
        return answered(asked, ShareReply::Bare{});
    if (share.kind(guest(*to)) != Kind::Absent)
        return failed(asked, ShareError::ObjectExists);
    if (!share.move(svc::MoveNames{.from = guest(*from), .to = guest(*to)})) {
        return failed(asked, ShareError::ObjectNotFound);
    }
    return answered(asked, ShareReply::Bare{});
}

ShareReply AmigaDosShare::create_dir_(const Request& asked, HostFileSandbox& share) {
    const auto path = find_path_(share, asked.lock, asked.name);
    if (!path)
        return answered(asked, ShareReply::LockAnswer{add_lock_(std::nullopt)});
    const Kind kind = share.kind(guest(*path));
    if (kind == Kind::File || (kind == Kind::Absent && !share.make_dir(guest(*path)))) {
        return failed(asked, ShareError::ObjectNotFound);
    }
    return answered(asked, ShareReply::LockAnswer{add_lock_(*path)});
}

ShareReply AmigaDosShare::info_(const Request& asked, const HostFileSandbox& share) const {
    const auto space = share.space();
    if (!space)
        return answered(asked, kNoVolume);
    return answered(asked, ShareReply::Volume{.total = space->total, .used = space->used()});
}

ShareReply AmigaDosShare::same_lock_(const Request& asked) const {
    const auto a = locks_.find(asked.lock);
    const auto b = locks_.find(asked.other);
    if (a == locks_.end() || b == locks_.end())
        return failed(asked, ShareError::LockDifferent);
    if (a->second.path == b->second.path)
        return answered(asked, ShareReply::Bare{});
    return failed(asked, ShareError::LockSameVolume);
}

infra::ByteCount AmigaDosShare::encode(const ShareReply& r, std::span<std::byte> out) const {
    TASTY_SEAT_BODY(AmigaDosShare);
    if (infra::ByteCount{out.size()} < kHeaderBytes)
        return infra::ByteCount{};
    if (r.terminator) {
        std::span<std::byte> term = infra::affine::from_clamped(out, *r.terminator);
        if (!term.empty())
            term[0] = std::byte{0};
    }
    const bool ok = r.error == ShareError::None;
    ShareWord::verdict(ok).store(out, kSuccessAt);
    ShareWord::of(r.error).store(out, kErrorAt);
    if (const std::optional<ShareWord> w = body_word(r.body)) {
        w->store(out, kBodyAt);
    } else if (const auto* v = std::get_if<ShareReply::Volume>(&r.body)) {
        if (infra::affine::fits(infra::ByteOffset{}, kVolumeBytes, infra::ByteCount{out.size()})) {
            ShareWord::of_blocks(v->total).store(out, kBodyAt);
            ShareWord::of_blocks(v->used).store(out, kVolumeUsedAt);
        }
    } else if (const auto* e = std::get_if<ShareReply::Examined>(&r.body)) {
        const infra::ByteOffset name_end = kExamineNameAt
            + infra::ByteCount{1}
            + infra::ByteCount{e->name.size()}
            + infra::ByteCount{1};
        if (infra::affine::fits(
                infra::ByteOffset{},
                name_end - infra::ByteOffset{},
                infra::ByteCount{out.size()}
            )) {
            ShareWord::of(e->cursor).store(out, kBodyAt);
            ShareWord::of(e->entry).store(out, kExamineTypeAt);
            ShareWord::of_held(e->size).store(out, kExamineSizeAt);
            ShareWord{}.store(out, kExamineProtectAt);
            ShareWord::of(e->date.days).store(out, kExamineDateAt);
            ShareWord::of(e->date.minutes).store(out, kExamineMinutesAt);
            ShareWord::of(e->date.ticks).store(out, kExamineTicksAt);
            infra::affine::from_clamped(out, kExamineNameAt)[0] = ShareWord::bcpl_length(e->name);
            std::memcpy(
                infra::affine::from_clamped(out, kExamineNameAt + infra::ByteCount{1}).data(),
                e->name.data(),
                e->name.size()
            );
            infra::affine::from_clamped(out, name_end - infra::ByteCount{1})[0] = std::byte{0};
        }
    }
    return reply_bytes(r);
}

}
