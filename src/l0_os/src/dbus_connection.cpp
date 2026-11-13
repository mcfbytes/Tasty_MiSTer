// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/dbus_connection.h"

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "dbus_wire_internal.h"

namespace mister::os {
namespace {

using namespace detail;

constexpr BufferOffset kBodyLengthAt{4};
constexpr BufferOffset kSerialAt{8};
constexpr BufferOffset kArrayLengthAt{12};

constexpr std::string_view kBusName = "org.freedesktop.DBus";
constexpr std::string_view kBusPath = "/org/freedesktop/DBus";
constexpr std::string_view kHelloMember = "Hello";

[[nodiscard]] Error format_error() noexcept {
    return Error{Errc::bad_format, ERR_SITE(), 0};
}

[[nodiscard]] Error busy_error() noexcept {
    return Error{Errc::busy, ERR_SITE(), 0};
}

[[nodiscard]] Error refused_error() noexcept {
    return Error{Errc::negotiation, ERR_SITE(), 0};
}

[[nodiscard]] Error short_write_error() noexcept {
    return Error{Errc::short_write, ERR_SITE(), 0};
}

enum class ByteOrder : std::uint8_t { Little, Big, Invalid };

[[nodiscard]] ByteOrder byte_order(WireU8 marker) noexcept {
    if (marker == kLittleEndianMarker)
        return ByteOrder::Little;
    if (marker == kBigEndianMarker)
        return ByteOrder::Big;
    return ByteOrder::Invalid;
}

}

DbusConnection::DbusConnection(UnixStreamSocket socket) noexcept : socket_(std::move(socket)) {}

Ex<std::optional<DbusConnection>> DbusConnection::open(std::string_view socket_path) noexcept {
    auto connected = UnixStreamSocket::connect(socket_path);
    if (!connected)
        return std::unexpected(connected.error());
    if (!connected->has_value())
        return std::optional<DbusConnection>{};
    DbusConnection connection{std::move(**connected)};
    auto started = connection.start();
    if (!started)
        return std::unexpected(started.error());
    return std::optional<DbusConnection>{std::move(connection)};
}

Ex<void> DbusConnection::start() noexcept {
    const std::span<const std::byte> pending = auth_.pending();
    if (WireBytes{pending.size()}
        > left_in(std::span<const std::byte>{arenas_->out}, BufferOffset::kStart)) {
        return std::unexpected(short_write_error());
    }
    std::memcpy(arenas_->out.data(), pending.data(), pending.size());
    out_begin_ = BufferOffset::kStart;
    begin_offset_ = as_offset(WireBytes{pending.size()});
    out_end_ = begin_offset_ + DbusAuth::kBeginBytes;
    if (out_end_ > end_of(std::span<const std::byte>{arenas_->out})) {
        return std::unexpected(short_write_error());
    }
    out_limit_ = begin_offset_;

    DbusHeader hello{};
    hello.type = DbusHeader::Type::MethodCall;
    hello.serial = DbusSerial::hello();
    auto path = hello.add_field(DbusHeaderField::Code::Path, kBusPath);
    if (!path)
        return std::unexpected(path.error());
    auto interface = hello.add_field(DbusHeaderField::Code::Interface, kBusName);
    if (!interface)
        return std::unexpected(interface.error());
    auto member = hello.add_field(DbusHeaderField::Code::Member, kHelloMember);
    if (!member)
        return std::unexpected(member.error());
    auto destination = hello.add_field(DbusHeaderField::Code::Destination, kBusName);
    if (!destination)
        return std::unexpected(destination.error());

    auto body_offset = hello.encode(out_end_.at(std::span<std::byte>{arenas_->out}));
    if (!body_offset)
        return std::unexpected(body_offset.error());
    out_end_ = advance(out_end_, *body_offset);
    return flush();
}

Ex<void> DbusConnection::flush() noexcept {
    if (!socket_)
        return std::unexpected(closed_error());
    while (out_begin_ < out_limit_) {
        auto written = socket_->write_some(
            out_begin_.up_to(out_limit_, std::span<const std::byte>{arenas_->out})
        );
        if (!written) {
            close();
            return std::unexpected(written.error());
        }
        if (written->empty())
            break;
        out_begin_ = advance(out_begin_, WireBytes{written->size()});
    }
    if (ready_ && out_begin_ == out_end_) {
        out_begin_ = BufferOffset::kStart;
        out_end_ = BufferOffset::kStart;
        out_limit_ = BufferOffset::kStart;
        begin_offset_ = BufferOffset::kStart;
    }
    return {};
}

Ex<void> DbusConnection::read_into_arena() noexcept {
    if (!socket_)
        return std::unexpected(closed_error());
    if (in_begin_ != BufferOffset::kStart) {

        const std::span<const std::byte> from = in_begin_.up_to(
            in_end_,
            std::span<const std::byte>{arenas_->in}
        );
        const std::span<std::byte> to = in_end_.since(in_begin_).head_of(
            std::span<std::byte>{arenas_->in}
        );
        if (!from.empty())
            std::memmove(to.data(), from.data(), from.size());
        in_begin_ = BufferOffset::kStart;
        in_end_ = advance(BufferOffset::kStart, WireBytes{from.size()});
    }
    if (in_end_ == end_of(std::span<const std::byte>{arenas_->in}))
        return {};
    auto read = socket_->read_some(in_end_.at(std::span<std::byte>{arenas_->in}));
    if (!read) {
        close();
        return std::unexpected(read.error());
    }
    if (!read->has_value())
        return {};
    if (read->value().empty()) {
        close();
        return std::unexpected(closed_error());
    }
    in_end_ = advance(in_end_, WireBytes{read->value().size()});
    return {};
}

Ex<std::optional<DbusIncoming>> DbusConnection::parse_frame() noexcept {
    const std::span<const std::byte> arena{arenas_->in};
    for (;;) {
        if (closed_)
            return std::unexpected(closed_error());
        if (skip_remaining_ != WireBytes::kNone) {
            const WireBytes drop = WireBytes::least(in_end_.since(in_begin_), skip_remaining_);
            in_begin_ = advance(in_begin_, drop);
            skip_remaining_ = skip_remaining_ - drop;
            if (skip_remaining_ != WireBytes::kNone)
                return std::optional<DbusIncoming>{};
            continue;
        }
        const WireBytes available = in_end_.since(in_begin_);
        if (available < DbusHeader::kFixedSize)
            return std::optional<DbusIncoming>{};
        const std::span<const std::byte> frame = in_begin_.up_to(in_end_, arena);
        const ByteOrder order = byte_order(as_wire(frame.front()));
        if (order == ByteOrder::Invalid) {
            close();
            return std::unexpected(format_error());
        }
        const bool big = order == ByteOrder::Big;
        const WireU32 header_length = load_u32(frame, kArrayLengthAt, big);
        const WireU32 body_length = load_u32(frame, kBodyLengthAt, big);
        if (as_bytes(header_length) > kMaxMessage || as_bytes(body_length) > kMaxMessage) {
            close();
            return std::unexpected(format_error());
        }

        const WireBytes padded = DbusHeader::kFixedSize + as_bytes(header_length);
        const BufferOffset body_offset = as_offset(
            padded + padding_to(as_offset(padded), DbusHeader::kMessageAlign)
        );
        const WireBytes total = DbusHeader::message_size(
            body_offset.since(BufferOffset::kStart),
            as_bytes(body_length)
        );
        if (total > kMaxMessage) {
            oversize_frames_ = oversize_frames_.next();
            close();
            return std::unexpected(format_error());
        }
        if (total > left_in(arena, BufferOffset::kStart)) {
            skipped_frames_ = skipped_frames_.next();
            skip_remaining_ = total - available;
            in_begin_ = in_end_;
            if (skip_remaining_ != WireBytes::kNone)
                return std::optional<DbusIncoming>{};
            continue;
        }
        if (available < total)
            return std::optional<DbusIncoming>{};

        auto header = DbusHeader::decode(
            (DbusHeader::kFixedSize + as_bytes(header_length)).head_of(frame)
        );
        if (!header) {
            close();
            return std::unexpected(header.error());
        }
        DbusIncoming incoming{};
        incoming.header = *header;
        incoming.body = body_offset.up_to(body_offset + as_bytes(header->body_length), frame);
        in_begin_ = advance(in_begin_, total);
        return std::optional<DbusIncoming>{incoming};
    }
}

Ex<std::optional<DbusIncoming>> DbusConnection::poll() noexcept {
    if (closed_)
        return std::unexpected(closed_error());
    auto flushed = flush();
    if (!flushed)
        return std::unexpected(flushed.error());

    if (!ready_) {
        if (auth_.rejected()) {
            close();
            return std::unexpected(refused_error());
        }
        auto read = read_into_arena();
        if (!read)
            return std::unexpected(read.error());
        while (!auth_.rejected() && !auth_.complete() && in_begin_ < in_end_) {
            const WireBytes used = auth_.consume(
                in_begin_.up_to(in_end_, std::span<const std::byte>{arenas_->in})
            );
            in_begin_ = advance(in_begin_, used);
            if (used == WireBytes::kNone)
                break;
        }
        if (auth_.rejected()) {
            close();
            return std::unexpected(refused_error());
        }
        if (auth_.state() == DbusAuth::State::SendBegin) {
            const std::span<const std::byte> begin = auth_.pending();
            std::memcpy(
                begin_offset_.at(std::span<std::byte>{arenas_->out}).data(),
                begin.data(),
                begin.size()
            );
            const BufferOffset end = advance(begin_offset_, WireBytes{begin.size()});
            if (end > out_end_)
                out_end_ = end;
            out_limit_ = out_end_;
            auth_.wrote(WireBytes{begin.size()});
            auto sent = flush();
            if (!sent)
                return std::unexpected(sent.error());
            ready_ = auth_.complete();
        }
        if (!ready_)
            return std::optional<DbusIncoming>{};
    }
    auto read = read_into_arena();
    if (!read)
        return std::unexpected(read.error());
    return parse_frame();
}

Ex<void> DbusConnection::append_message(
    const DbusHeader& header,
    std::span<const std::byte> body
) noexcept {
    if (closed_)
        return std::unexpected(closed_error());
    if (WireBytes{body.size()} != as_bytes(header.body_length)) {
        return std::unexpected(format_error());
    }
    auto body_offset = header.encode(out_end_.at(std::span<std::byte>{arenas_->out}));
    if (!body_offset)
        return std::unexpected(body_offset.error());
    const WireBytes total = DbusHeader::message_size(*body_offset, as_bytes(header.body_length));
    if (total > left_in(std::span<const std::byte>{arenas_->out}, out_end_)) {
        return std::unexpected(busy_error());
    }
    std::memcpy(
        advance(out_end_, *body_offset).at(std::span<std::byte>{arenas_->out}).data(),
        body.data(),
        body.size()
    );
    out_end_ = advance(out_end_, total);
    if (ready_)
        out_limit_ = out_end_;
    return {};
}

Ex<DbusSerial> DbusConnection::call(const DbusMessage& message) noexcept {
    if (closed_)
        return std::unexpected(closed_error());
    if (WireBytes{message.body.size()} > kMaxMessage)
        return std::unexpected(format_error());
    DbusHeader header = message.header;
    header.serial = next_serial_;
    header.body_length = as_u32(WireBytes{message.body.size()});
    auto appended = append_message(header, message.body);
    if (!appended)
        return std::unexpected(appended.error());
    next_serial_ = next_serial_.next();
    auto flushed = flush();
    if (!flushed)
        return std::unexpected(flushed.error());
    return header.serial;
}

Ex<void> DbusConnection::reply(const DbusIncoming& request, const DbusMessage& message) noexcept {
    if (closed_)
        return std::unexpected(closed_error());
    if (WireBytes{message.body.size()} > kMaxMessage)
        return std::unexpected(format_error());
    DbusHeader header = message.header;
    if (header.type == DbusHeader::Type::MethodCall) {
        header.type = DbusHeader::Type::MethodReturn;
    }
    header.serial = next_serial_;
    header.body_length = as_u32(WireBytes{message.body.size()});
    const std::string_view sender = request.header.sender();
    if (!sender.empty()) {
        auto destination = header.add_field(DbusHeaderField::Code::Destination, sender);
        if (!destination)
            return std::unexpected(destination.error());
    }
    auto reply_to = header.add_reply_serial(request.header.serial);
    if (!reply_to)
        return std::unexpected(reply_to.error());
    auto appended = append_message(header, message.body);
    if (!appended)
        return std::unexpected(appended.error());
    next_serial_ = next_serial_.next();
    auto flushed = flush();
    if (!flushed)
        return std::unexpected(flushed.error());
    return {};
}

void DbusConnection::close() noexcept {
    socket_.reset();
    closed_ = true;
}

Error DbusConnection::closed_error() const noexcept {
    return infra::errno_error(Errc::os, ERR_SITE(), ECONNRESET);
}

const UniqueFd* DbusConnection::fd() const noexcept {
    return socket_ ? &socket_->fd() : nullptr;
}

}
