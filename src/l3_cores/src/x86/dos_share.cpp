// SPDX-License-Identifier: GPL-3.0-or-later
#include "cores/x86/dos_share.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <optional>
#include <string_view>
#include <utility>

#include "infra/affine.h"
#include "infra/byte_view.h"
#include "infra/endian.h"
#include "infra/span_cut.h"
#include "svc/dir_entry.h"
#include "svc/move_names.h"

namespace mister::cores::x86 {

namespace {

using Kind = HostFileSandbox::Kind;

constexpr infra::ByteCount kObjectNameAt{0};
constexpr infra::ByteCount kRenameLengthAt{0};
constexpr infra::ByteCount kRenameNameAt{1};
constexpr infra::ByteCount kAttrWordAt{0};
constexpr infra::ByteCount kActionWordAt{2};
constexpr infra::ByteCount kModeWordAt{4};
constexpr infra::ByteCount kOpenNameAt{6};
constexpr infra::ByteCount kFindTokenAt{0};
constexpr infra::ByteCount kFindAttrAt{2};
constexpr infra::ByteCount kFindNameAt{3};
constexpr infra::ByteCount kFindCursorAt{2};
constexpr infra::ByteCount kCloseKeyAt{0};
constexpr infra::ByteCount kIoAtAt{0};
constexpr infra::ByteCount kIoKeyAt{4};
constexpr infra::ByteCount kIoCountAt{6};
constexpr infra::ByteCount kWriteDataAt{8};
constexpr MailboxOffset kWriteWindowAt{8};
constexpr std::string_view kWildcards = "?*";

[[nodiscard]] std::string text_at(
    std::span<const std::byte> packet,
    infra::ByteOffset at,
    infra::ByteCount most
) {
    std::string out;
    for (const std::byte b : most.first_in(infra::affine::from_clamped(packet, at))) {
        const char c = std::to_integer<char>(b);
        if (c == '\0')
            break;
        out.push_back(c);
    }
    return out;
}

[[nodiscard]] std::byte byte_at(std::span<const std::byte> packet, infra::ByteOffset at) noexcept {
    const std::span<const std::byte> rest = infra::affine::from_clamped(packet, at);
    return rest.empty() ? std::byte{0} : rest[0];
}

[[nodiscard]] std::span<const std::byte> bytes_at(
    std::span<const std::byte> packet,
    infra::ByteOffset at
) noexcept {
    return infra::affine::from_clamped(packet, at);
}

[[nodiscard]] svc::OpenMode access_mode(DosOpenMode::Access access) noexcept {
    switch (access) {
        case DosOpenMode::Access::Read:
            return svc::OpenMode::Read;
        case DosOpenMode::Access::Write:
        case DosOpenMode::Access::ReadWrite:
            return svc::OpenMode::ReadWrite;
        case DosOpenMode::Access::Neither:
            return svc::OpenMode::Read;
    }
    return svc::OpenMode::Read;
}

void store_cut(
    std::span<std::byte> packet_out,
    const std::optional<infra::ByteOffset>& at
) noexcept {
    if (!at)
        return;
    const std::span<std::byte> cut = infra::affine::from_clamped(packet_out, *at);
    if (!cut.empty())
        cut[0] = std::byte{0};
}

[[nodiscard]] infra::ByteCount write_body(
    const DosShareReply::Body& body,
    std::span<std::byte> payload
) {
    using BodyKind = DosBodyKind;
    switch (dos_body_kind(body)) {
        case BodyKind::Nothing:
            return {};
        case BodyKind::Written:
            if (const auto* arm = std::get_if<DosShareReply::Written>(&body)) {
                arm->store(payload);
                return arm->length();
            }
            break;
        case BodyKind::Space:
            if (const auto* arm = std::get_if<DosShareReply::Space>(&body)) {
                arm->store(payload);
                return arm->length();
            }
            break;
        case BodyKind::Attribute:
            if (const auto* arm = std::get_if<DosShareReply::Attribute>(&body)) {
                arm->store(payload);
                return arm->length();
            }
            break;
        case BodyKind::Opened:
            if (const auto* arm = std::get_if<DosShareReply::Opened>(&body)) {
                arm->store(payload);
                return arm->length();
            }
            break;
        case BodyKind::Found:
            if (const auto* arm = std::get_if<DosShareReply::Found>(&body)) {
                arm->store(payload);
                return arm->length();
            }
            break;
        case BodyKind::Position:
            if (const auto* arm = std::get_if<DosShareReply::Position>(&body)) {
                arm->store(payload);
                return arm->length();
            }
            break;
        case BodyKind::Data:
            if (const auto* arm = std::get_if<DosShareReply::Data>(&body))
                return arm->length();
            break;
        case BodyKind::Stale:
            if (const auto* arm = std::get_if<DosShareReply::Stale>(&body))
                return arm->length();
            break;
        case BodyKind::kCount:
            break;
    }
    std::unreachable();
}

[[nodiscard]] constexpr char upper(char c) noexcept {
    constexpr std::string_view kLower = "abcdefghijklmnopqrstuvwxyz";
    constexpr std::string_view kUpper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const std::size_t at = kLower.find(c);
    return at == std::string_view::npos ? c : kUpper[at];
}

}

DosStamp DosShare::stamp_of_(const HostFileSandbox& share, std::string_view path) const noexcept {
    if (const auto when = share.modified(path))
        return DosStamp::of(*when);
    return {};
}

proto::FileSize DosShare::size_of_(const HostFileSandbox& share, const DosPath& path) const {
    const auto entries = share.list(path.parent());
    if (!entries)
        return {};
    const std::string_view leaf = path.leaf();
    for (const svc::DirEntry& entry : *entries) {
        if (std::string_view{entry.name} == leaf)
            return entry.size;
    }
    return {};
}

DosStamp DosStamp::of(std::chrono::sys_seconds when) noexcept {
    std::tm local{};
    const std::time_t clock = when.time_since_epoch().count();
    if (::localtime_r(&clock, &local) == nullptr)
        return {};
    std::uint32_t word = 0;
    word = kSeconds.set(
        word,
        Field::Value{static_cast<std::uint32_t>(local.tm_sec) / kSecondsStep}
    );
    word = kMinutes.set(word, Field::Value{static_cast<std::uint32_t>(local.tm_min)});
    word = kHours.set(word, Field::Value{static_cast<std::uint32_t>(local.tm_hour)});
    word = kDay.set(word, Field::Value{static_cast<std::uint32_t>(local.tm_mday)});
    word = kMonth.set(word, Field::Value{static_cast<std::uint32_t>(local.tm_mon + 1)});
    word = kYear.set(word, Field::Value{static_cast<std::uint32_t>(local.tm_year - kTmYearBase)});
    return DosStamp{word};
}

Name83 Name83::of(std::string_view host_name) noexcept {
    Name83 out{};
    out.text.fill(kPad);
    const std::size_t slash = host_name.rfind('/');
    const std::string_view leaf = slash == std::string_view::npos ? host_name
                                                                  : host_name.substr(slash + 1);
    std::string_view name = leaf;
    std::string_view ext{};
    if (leaf != "." && leaf != "..") {
        const std::size_t dot = leaf.rfind('.');
        if (dot != std::string_view::npos) {
            name = leaf.substr(0, dot);
            ext = leaf.substr(dot + 1);
        }
    }
    const std::size_t name_len = std::min(name.size(), kNameBytes);
    for (std::size_t i = 0; i < name_len; ++i)
        out.text[i] = upper(name[i]);
    const std::size_t ext_len = std::min(ext.size(), kExtBytes);
    for (std::size_t i = 0; i < ext_len; ++i)
        out.text[kNameBytes + i] = upper(ext[i]);
    return out;
}

bool Name83::matches(std::string_view host_name) const noexcept {
    const std::size_t dot = host_name.rfind('.');
    const std::size_t name_len = dot == std::string_view::npos ? host_name.size() : dot;
    const std::size_t ext_len = dot == std::string_view::npos ? 0 : host_name.size() - dot - 1;
    if (name_len > kNameBytes || ext_len > kExtBytes)
        return false;
    const Name83 candidate = of(host_name);
    bool starred = false;
    for (std::size_t i = 0; i < kBytes; ++i) {
        if (i == kNameBytes)
            starred = false;
        if (starred)
            continue;
        const char filter = text[i];
        if (filter == '?')
            continue;
        if (filter == '*') {
            starred = true;
            continue;
        }
        if (filter != candidate.text[i])
            return false;
    }
    return true;
}

void Name83::store(std::span<std::byte, kBytes> out) const noexcept {
    std::memcpy(out.data(), text.data(), text.size());
}

DosPath DosPath::of(std::string_view guest_name) {
    const std::size_t colon = guest_name.find(':');
    const std::string_view after = colon == std::string_view::npos ? guest_name
                                                                   : guest_name.substr(colon + 1);
    std::string text;
    text.reserve(after.size());
    for (const char c : after)
        text.push_back(c == '\\' ? '/' : c);
    const std::size_t first = text.find_first_not_of('/');
    if (first == std::string::npos)
        return DosPath{std::string{kRoot}};
    return DosPath{text.substr(first)};
}

std::string_view DosPath::parent() const noexcept {
    std::string_view text = text_;
    while (text.size() > 1 && text.back() == '/')
        text.remove_suffix(1);
    const std::size_t cut = text.rfind('/');
    return cut == std::string_view::npos ? std::string_view{kRoot} : text.substr(0, cut);
}

std::string_view DosPath::leaf() const noexcept {
    std::string_view text = text_;
    while (text.size() > 1 && text.back() == '/')
        text.remove_suffix(1);
    const std::size_t cut = text.rfind('/');
    return cut == std::string_view::npos ? text : text.substr(cut + 1);
}

std::string DosPath::under(std::string_view name) const {
    if (is_root())
        return std::string{name};
    std::string out{text_};
    out.push_back('/');
    out.append(name);
    return out;
}

Ex<DosShare::Request> DosShare::decode(std::span<const std::byte> packet) const {
    TASTY_SEAT_BODY(DosShare);
    if (infra::ByteCount{packet.size()} < kHeaderSize) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    const std::uint16_t declared = infra::load_le<std::uint16_t>(packet.first<2>());
    const infra::ByteCount length{declared};
    if (length >= infra::ByteCount{kDataLength}) {
        return std::unexpected(Error{Errc::bad_format, ERR_SITE(), 0});
    }
    Request r;
    r.op = dos_share_op_of(infra::affine::from(packet, kFuncAt)[0]);
    r.length = MailboxLen{declared};
    r.terminator = kPayloadAt + length;
    if (!r.op)
        return r;
    switch (*r.op) {
        case DosShareOp::Rmdir:
        case DosShareOp::Mkdir:
        case DosShareOp::Chdir:
        case DosShareOp::GetAttr:
        case DosShareOp::Delete:
            r.path = DosPath::of(text_at(
                packet,
                kPayloadAt + kObjectNameAt,
                infra::affine::saturating_sub(length, kObjectNameAt)
            ));
            break;
        case DosShareOp::Open:
            r.mode = DosOpenMode::of_bits(byte_at(packet, kPayloadAt + kAttrWordAt));
            r.path = DosPath::of(text_at(
                packet,
                kPayloadAt + kOpenNameAt,
                infra::affine::saturating_sub(length, kOpenNameAt)
            ));
            break;
        case DosShareOp::Create:
            r.path = DosPath::of(text_at(
                packet,
                kPayloadAt + kOpenNameAt,
                infra::affine::saturating_sub(length, kOpenNameAt)
            ));
            break;
        case DosShareOp::SpOpen:
            r.attr = DosAttr::of(byte_at(packet, kPayloadAt + kAttrWordAt));
            r.action = Request::SpOpenAction::of(bytes_at(packet, kPayloadAt + kActionWordAt));
            r.mode = DosOpenMode::decode(bytes_at(packet, kPayloadAt + kModeWordAt));
            r.path = DosPath::of(text_at(
                packet,
                kPayloadAt + kOpenNameAt,
                infra::affine::saturating_sub(length, kOpenNameAt)
            ));
            break;
        case DosShareOp::Close:
            r.file = ShareFileKey::decode(bytes_at(packet, kPayloadAt + kCloseKeyAt));
            break;
        case DosShareOp::Read:
        case DosShareOp::Write:
            r.file = ShareFileKey::decode(bytes_at(packet, kPayloadAt + kIoKeyAt));
            r.at = DosLong::decode(bytes_at(packet, kPayloadAt + kIoAtAt)).offset();
            r.count = DosWord::decode(bytes_at(packet, kPayloadAt + kIoCountAt)).bytes();
            break;
        case DosShareOp::SkfMend:
            r.file = ShareFileKey::decode(bytes_at(packet, kPayloadAt + kIoKeyAt));
            r.displacement = Request::Displacement::of(bytes_at(packet, kPayloadAt + kIoAtAt));
            break;
        case DosShareOp::Rename: {
            const std::uint8_t source_len = std::to_integer<std::uint8_t>(
                byte_at(packet, kPayloadAt + kRenameLengthAt)
            );
            const infra::ByteCount source_at = kRenameNameAt;
            r.path = DosPath::of(text_at(
                packet,
                kPayloadAt + source_at,
                std::min(
                    infra::affine::saturating_sub(length, source_at),
                    infra::ByteCount{source_len}
                )
            ));
            const infra::ByteCount target_at = kRenameNameAt + infra::ByteCount{source_len};
            r.target = DosPath::of(text_at(
                packet,
                kPayloadAt + target_at,
                infra::affine::saturating_sub(length, target_at)
            ));
            r.split = kPayloadAt + target_at;
            break;
        }
        case DosShareOp::FindFirst:
            r.token = DosToken::decode(bytes_at(packet, kPayloadAt + kFindTokenAt));
            r.attr = DosAttr::of(byte_at(packet, kPayloadAt + kFindAttrAt));
            r.path = DosPath::of(text_at(
                packet,
                kPayloadAt + kFindNameAt,
                infra::affine::saturating_sub(length, kFindNameAt)
            ));
            break;
        case DosShareOp::FindNext:
            r.lock = ShareLockKey::decode(bytes_at(packet, kPayloadAt + kCloseKeyAt));
            r.cursor = DosCursor::decode(bytes_at(packet, kPayloadAt + kFindCursorAt));
            break;
        case DosShareOp::Lock:
        case DosShareOp::DiskSpace:
        case DosShareOp::SetAttr:
        case DosShareOp::Qualify:
            break;
    }
    return r;
}

void DosShare::reset() noexcept {
    TASTY_SEAT_BODY(DosShare);
    locks_.clear();
    files_.clear();
    next_lock_ = ShareLockKey{ShareLockKey::kFirst};
    next_file_ = ShareFileKey{ShareFileKey::kFirst};
}

ShareLockKey DosShare::add_lock_(DosToken token) {
    for (auto& [key, lock] : locks_) {
        if (lock.token == token) {
            lock.items.clear();
            return key;
        }
    }
    ShareLockKey key = next_lock_.next();
    while (locks_.contains(key))
        key = key.next();
    next_lock_ = key;
    locks_[key] = Lock{.token = token};
    return key;
}

ShareFileKey DosShare::take_file_key_() {
    ShareFileKey key = next_file_.next();
    while (files_.contains(key))
        key = key.next();
    next_file_ = key;
    return key;
}

std::optional<std::string> DosShare::find_path_(
    const HostFileSandbox& share,
    const DosPath& path
) const {
    if (share.kind(path.parent()) != Kind::Dir)
        return std::nullopt;
    if (!share.resolve(path.view()))
        return std::nullopt;
    return std::string{path.view()};
}

Ex<DosShare::Reply> DosShare::serve(Request& asked, HostFileSandbox& share, MailboxWindow& window) {
    TASTY_SEAT_BODY(DosShare);
    Reply answered = [&]() -> Reply {
        if (!asked.op)
            return failed_(DosError::Unknown);
        switch (*asked.op) {
            case DosShareOp::Rmdir:
                return rmdir_(asked, share);
            case DosShareOp::Mkdir:
                return mkdir_(asked, share);
            case DosShareOp::Chdir:
                return chdir_(asked, share);
            case DosShareOp::Close:
                return close_(asked);
            case DosShareOp::Read:
                return read_(asked, window);
            case DosShareOp::Write:
                return write_(asked, window);
            case DosShareOp::Lock:
                return ok_();
            case DosShareOp::DiskSpace:
                return disk_space_(share);
            case DosShareOp::SetAttr:
                return ok_();
            case DosShareOp::GetAttr:
                return get_attr_(asked, share);
            case DosShareOp::Rename:
                return rename_(asked, share);
            case DosShareOp::Delete:
                return delete_(asked, share);
            case DosShareOp::Open:
                return open_(asked, share);
            case DosShareOp::Create:
                return create_(asked, share);
            case DosShareOp::FindFirst:
                return find_first_(asked, share);
            case DosShareOp::FindNext:
                return find_next_(asked);
            case DosShareOp::SkfMend:
                return skf_mend_(asked);
            case DosShareOp::Qualify:
                return qualify_();
            case DosShareOp::SpOpen:
                return sp_open_(asked, share);
        }
        return failed_(DosError::Unknown);
    }();
    answered.terminator = asked.terminator;
    return answered;
}

DosShare::Reply DosShare::ok_() noexcept {
    return Reply{};
}

DosShare::Reply DosShare::failed_(DosError error) noexcept {
    return Reply{.result = dos_error(error)};
}

DosShare::Reply DosShare::qualify_() noexcept {
    return Reply{.body = Reply::Stale{}};
}

DosShare::Reply DosShare::rmdir_(const Request& asked, HostFileSandbox& share) {
    const auto path = find_path_(share, asked.path);
    if (!path)
        return failed_(DosError::PathNotFound);
    return share.remove_dir(*path) ? ok_() : failed_(DosError::WriteFault);
}

DosShare::Reply DosShare::mkdir_(const Request& asked, HostFileSandbox& share) {
    const auto path = find_path_(share, asked.path);
    if (!path)
        return failed_(DosError::PathNotFound);

    const Kind kind = share.kind(*path);
    if (kind == Kind::Dir)
        return ok_();
    if (kind == Kind::File)
        return failed_(DosError::WriteFault);
    return share.make_dir(*path) ? ok_() : failed_(DosError::WriteFault);
}

DosShare::Reply DosShare::chdir_(const Request& asked, HostFileSandbox& share) {
    const auto path = find_path_(share, asked.path);
    if (!path)
        return failed_(DosError::PathNotFound);
    return share.kind(*path) == Kind::Dir ? ok_() : failed_(DosError::PathNotFound);
}

DosShare::Reply DosShare::close_(const Request& asked) {
    files_.erase(asked.file);
    return ok_();
}

DosShare::Reply DosShare::read_(const Request& asked, const MailboxWindow& window) {
    const auto it = files_.find(asked.file);
    if (it == files_.end())
        return failed_(DosError::AccessDenied);
    OpenFile& open = it->second;
    const infra::ByteCount room = std::min(asked.count, infra::ByteCount{window.size()});
    if (room.empty())
        return Reply{.body = Reply::Data{}};
    if (!dos_access_reads(open.access))
        return failed_(DosError::AccessDenied);
    const auto dst = window.at(MailboxSpan{.off = {}, .len = mailbox_len(room)});
    if (!dst)
        return failed_(DosError::AccessDenied);
    const auto got = open.file->read_at(asked.at, *dst);
    if (!got)
        return failed_(DosError::AccessDenied);
    return Reply{.body = Reply::Data{.count = *got}};
}

DosShare::Reply DosShare::write_(const Request& asked, const MailboxWindow& window) {
    const auto it = files_.find(asked.file);
    if (it == files_.end())
        return failed_(DosError::AccessDenied);
    OpenFile& open = it->second;
    const infra::ByteCount room = infra::affine::saturating_sub(
        infra::ByteCount{window.size()},
        kWriteDataAt
    );
    const infra::ByteCount want = std::min(asked.count, room);
    if (want.empty())
        return Reply{.body = Reply::Written{}};
    if (!dos_access_writes(open.access))
        return failed_(DosError::AccessDenied);
    const auto src = window.at(MailboxSpan{.off = kWriteWindowAt, .len = mailbox_len(want)});
    if (!src)
        return failed_(DosError::AccessDenied);
    if (!open.file->write_all_at(asked.at, *src))
        return failed_(DosError::AccessDenied);
    return Reply{.body = Reply::Written{.count = DosWord::of(want)}};
}

DosShare::Reply DosShare::disk_space_(const HostFileSandbox& share) const {
    const DosResult result = dos_ax(DosAx{kSectorsPerCluster});
    const auto space = share.space();
    if (!space) {
        return Reply{
            .result = result,
            .body = Reply::Space{
                .total = kNoVolumeTotal,
                .bytes_per_sector = kStandardSectorBytes,
                .available = kNoVolumeAvailable
            }
        };
    }
    return Reply{
        .result = result,
        .body = Reply::Space{
            .total = Reply::Clusters::of_blocks(space->total),
            .bytes_per_sector = kStandardSectorBytes,
            .available = Reply::Clusters::of_blocks(space->available)
        }
    };
}

DosShare::Reply DosShare::get_attr_(const Request& asked, HostFileSandbox& share) const {
    const auto path = find_path_(share, asked.path);
    if (!path)
        return failed_(DosError::FileNotFound);
    const Kind kind = share.kind(*path);
    if (kind == Kind::Absent)
        return failed_(DosError::FileNotFound);
    const auto when = share.modified(*path);
    if (!when)
        return failed_(DosError::FileNotFound);
    const proto::FileSize size = kind == Kind::File ? size_of_(share, asked.path)
                                                    : proto::FileSize{};
    return Reply{
        .body = Reply::Attribute{
            .stamp = DosStamp::of(*when),
            .size = size,
            .attr = kind == Kind::Dir ? kDosAttrDirectory : DosAttr{}
        }
    };
}

DosShare::Reply DosShare::rename_(const Request& asked, HostFileSandbox& share) {
    const auto to = find_path_(share, asked.target);
    if (!to)
        return failed_(DosError::PathNotFound);

    Reply answered = ok_();
    answered.split = asked.split;
    const auto from = find_path_(share, asked.path);
    if (!from) {
        answered.result = dos_error(DosError::FileNotFound);
        return answered;
    }
    if (!share.move(svc::MoveNames{.from = *from, .to = *to})) {
        answered.result = dos_error(DosError::AccessDenied);
        return answered;
    }
    return answered;
}

DosShare::Reply DosShare::delete_(const Request& asked, HostFileSandbox& share) {
    const auto path = find_path_(share, asked.path);
    if (!path)
        return failed_(DosError::PathNotFound);
    if (asked.path.view().find_first_of(kWildcards) == std::string_view::npos) {
        if (share.kind(*path) == Kind::Absent)
            return failed_(DosError::FileNotFound);
        return share.remove(*path) ? ok_() : failed_(DosError::FileNotFound);
    }
    const DosPath dir = DosPath::of(asked.path.parent());
    const auto entries = share.list(dir.view());
    if (!entries)
        return failed_(DosError::FileNotFound);
    const Name83 filter = Name83::of(asked.path.leaf());
    bool found = false;
    bool refused = false;
    for (const svc::DirEntry& entry : *entries) {
        if (!filter.matches(entry.name))
            continue;
        if (share.remove(dir.under(entry.name)))
            found = true;
        else
            refused = true;
    }
    return found && !refused ? ok_() : failed_(DosError::FileNotFound);
}

DosShare::Reply DosShare::open_(const Request& asked, HostFileSandbox& share) {
    const auto path = find_path_(share, asked.path);
    if (!path)
        return failed_(DosError::PathNotFound);
    if (share.kind(*path) != Kind::File)
        return failed_(DosError::FileNotFound);
    const DosOpenMode::Access access = asked.mode.access();
    auto file = share.open(*path, access_mode(access));
    if (!file)
        return failed_(DosError::AccessDenied);
    const ShareFileKey key = take_file_key_();
    const auto size = (*file)->size();
    files_[key] = OpenFile{.file = std::move(*file), .access = access};
    return Reply{
        .body = Reply::Opened{
            .name = Name83::of(asked.path.leaf()),
            .stamp = stamp_of_(share, *path),
            .size = size ? *size : proto::FileSize{},
            .key = key,
            .extra = kSpOpenPlain,
            .mode = asked.mode
        }
    };
}

DosShare::Reply DosShare::create_(const Request& asked, HostFileSandbox& share) {
    const auto path = find_path_(share, asked.path);
    if (!path)
        return failed_(DosError::PathNotFound);
    auto file = share.open(*path, svc::OpenMode::Truncate);
    if (!file)
        return failed_(DosError::AccessDenied);
    const ShareFileKey key = take_file_key_();
    const auto size = (*file)->size();
    files_[key] = OpenFile{.file = std::move(*file), .access = DosOpenMode::Access::ReadWrite};
    return Reply{
        .body = Reply::Opened{
            .name = Name83::of(asked.path.leaf()),
            .stamp = stamp_of_(share, *path),
            .size = size ? *size : proto::FileSize{},
            .key = key,
            .extra = kSpOpenPlain,
            .mode = kDosOpenReadWrite
        }
    };
}

DosShare::Reply DosShare::sp_open_(const Request& asked, HostFileSandbox& share) {
    const auto path = find_path_(share, asked.path);
    if (!path)
        return failed_(DosError::PathNotFound);
    svc::OpenMode mode = svc::OpenMode::Read;
    DosOpenMode::Access access = DosOpenMode::Access::ReadWrite;
    DosWord extra = kSpOpenPlain;
    if (share.kind(*path) == Kind::File) {
        switch (asked.action.existing) {
            case Request::Existing::Open: {
                access = asked.mode.access();
                mode = access_mode(access);
                extra = kSpOpenOpened;
                break;
            }
            case Request::Existing::Truncate:
                mode = svc::OpenMode::Truncate;
                extra = kSpOpenTruncated;
                break;
            case Request::Existing::Fail:
                return failed_(DosError::AccessDenied);
        }
    } else {
        if (asked.action.missing != Request::Missing::Create || asked.attr.has(kDosAttrHidden)) {
            return failed_(DosError::FileNotFound);
        }
        mode = svc::OpenMode::Create;
        extra = kSpOpenCreated;
    }
    auto file = share.open(*path, mode);
    if (!file)
        return failed_(DosError::AccessDenied);
    const ShareFileKey key = take_file_key_();
    const auto size = (*file)->size();
    files_[key] = OpenFile{.file = std::move(*file), .access = access};
    return Reply{
        .body = Reply::Opened{
            .name = Name83::of(asked.path.leaf()),
            .stamp = stamp_of_(share, *path),
            .size = size ? *size : proto::FileSize{},
            .key = key,
            .extra = extra,
            .mode = asked.mode
        }
    };
}

DosShare::Reply DosShare::find_first_(const Request& asked, HostFileSandbox& share) {
    const auto path = find_path_(share, asked.path);
    if (!path)
        return failed_(DosError::NoMoreFiles);
    const ShareLockKey key = add_lock_(asked.token);
    if (asked.attr == kDosAttrVolume) {
        locks_[key].items.push_back(Item{.name = Name83::volume_label()});
        return Reply{
            .body = Reply::Found{
                .attr = kDosAttrVolume,
                .name = Name83::volume_label(),
                .stamp = {},
                .size = {},
                .key = key,
                .cursor = {}
            }
        };
    }
    const auto entries = share.list(asked.path.parent());
    if (!entries) {
        locks_.erase(key);
        return failed_(DosError::NoMoreFiles);
    }
    const DosPath dir = DosPath::of(asked.path.parent());
    const Name83 filter = Name83::of(asked.path.leaf());

    const bool with_dots = asked.attr.has(kDosAttrDirectory);
    for (const std::string_view name : {std::string_view{"."}, std::string_view{".."}}) {
        if (!with_dots || !filter.matches(name))
            continue;
        const std::string child = dir.under(name);
        const Kind special_kind = share.kind(child);
        Item item{.name = Name83::of(name), .attr = kDosAttrDirectory};
        if (special_kind != Kind::Absent) {
            if (const auto when = share.modified(child))
                item.stamp = DosStamp::of(*when);
        }
        locks_[key].items.push_back(std::move(item));
    }
    for (const svc::DirEntry& entry : *entries) {
        const std::string child = dir.under(entry.name);
        const Kind kind = share.kind(child);
        if (kind == Kind::Absent)
            continue;
        if (kind == Kind::Dir && !asked.attr.has(kDosAttrDirectory))
            continue;
        if (!filter.matches(entry.name))
            continue;
        const auto when = share.modified(child);
        if (!when)
            continue;
        locks_[key].items.push_back(
            Item{
                .name = Name83::of(entry.name),
                .stamp = DosStamp::of(*when),
                .size = entry.size,
                .attr = kind == Kind::Dir ? kDosAttrDirectory : DosAttr{}
            }
        );
    }
    return item_reply_(key, DosCursor{});
}

DosShare::Reply DosShare::find_next_(const Request& asked) {
    if (!locks_.contains(asked.lock))
        return failed_(DosError::NoMoreFiles);
    return item_reply_(asked.lock, asked.cursor.next());
}

DosShare::Reply DosShare::item_reply_(ShareLockKey key, DosCursor cursor) {
    const auto it = locks_.find(key);
    if (it == locks_.end())
        return failed_(DosError::NoMoreFiles);
    if (const Item* item = cursor.in<Item>(it->second.items); item != nullptr) {
        return Reply{
            .body = Reply::Found{
                .attr = item->attr,
                .name = item->name,
                .stamp = item->stamp,
                .size = item->size,
                .key = key,
                .cursor = cursor
            }
        };
    }
    locks_.erase(it);
    return failed_(DosError::NoMoreFiles);
}

DosShare::Reply DosShare::skf_mend_(const Request& asked) const {
    const auto it = files_.find(asked.file);
    if (it == files_.end())
        return failed_(DosError::FileNotFound);
    const auto size = it->second.file->size();
    if (!size)
        return failed_(DosError::FileNotFound);
    return Reply{.body = Reply::Position{.at = DosLong::of(asked.displacement.from(*size))}};
}

infra::ByteCount DosShare::encode(const Reply& answered, std::span<std::byte> packet_out) const {
    TASTY_SEAT_BODY(DosShare);
    if (infra::ByteCount{packet_out.size()} < kHeaderSize)
        return {};
    store_cut(packet_out, answered.terminator);
    store_cut(packet_out, answered.split);
    store_dos_result(answered.result, infra::affine::from(packet_out, kResultAt).first<2>());
    const std::span<std::byte> payload = infra::affine::from(packet_out, kPayloadAt);
    const infra::ByteCount length = write_body(answered.body, payload);
    DosWord::of(length).store(infra::affine::from(packet_out, kLengthAt).first<2>());
    return length;
}

}
