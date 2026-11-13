// SPDX-License-Identifier: GPL-3.0-or-later
#include "infra/byte_view.h"
#include "infra/error.h"
#include "infra/seat.h"
#include "os/dbus_bus_name.h"
#include "os/dbus_header.h"
#include "os/dbus_object_path.h"
#include "os/dbus_reader.h"
#include "os/dbus_writer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

#include "dbus_wire_internal.h"

namespace mister::os {
namespace {

using namespace detail;

constexpr WireU32 kWireTrue{1};
constexpr WireU32 kWireFalse{0};
constexpr BufferOffset kBodyLengthAt{4};
constexpr BufferOffset kFlagsAt{2};
constexpr BufferOffset kVersionAt{3};
constexpr BufferOffset kSerialAt{8};

[[nodiscard]] Error format_error() noexcept {
    return Error{Errc::bad_format, ERR_SITE(), 0};
}

[[nodiscard]] Error short_read_error() noexcept {
    return Error{Errc::short_read, ERR_SITE(), 0};
}

[[nodiscard]] bool is_lower(char c) noexcept {
    return c >= 'a' && c <= 'z';
}

[[nodiscard]] bool is_upper(char c) noexcept {
    return c >= 'A' && c <= 'Z';
}

[[nodiscard]] bool is_digit(char c) noexcept {
    return c >= '0' && c <= '9';
}

[[nodiscard]] bool is_word(char c) noexcept {
    return is_lower(c) || is_upper(c) || is_digit(c);
}

[[nodiscard]] bool is_element_char(char c) noexcept {
    return is_word(c) || c == '_';
}

[[nodiscard]] bool is_name_char(char c) noexcept {
    return is_word(c) || c == '_' || c == '-';
}

[[nodiscard]] bool is_basic(char c) noexcept {
    switch (c) {
        case kByte:
        case kBoolean:
        case kInt16:
        case kUint16:
        case kInt32:
        case kUint32:
        case kInt64:
        case kUint64:
        case kString:
        case kObjectPath:
        case kSignature:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] std::span<const char> chars_of(std::string_view text) noexcept {
    return std::span<const char>{text};
}

[[nodiscard]] std::optional<BufferOffset> parse_complete(
    std::span<const char> text,
    BufferOffset at,
    bool allow_dict
) noexcept {
    if (at >= end_of(text))
        return std::nullopt;
    const char code = text_at(text, at);
    if (code == kArray)
        return parse_complete(text, advance(at, WireU8::kWidth), true);
    if (code == kDictOpen) {
        if (!allow_dict)
            return std::nullopt;
        at = advance(at, WireU8::kWidth);
        if (at >= end_of(text) || !is_basic(text_at(text, at)))
            return std::nullopt;
        at = advance(at, WireU8::kWidth);
        const auto value = parse_complete(text, at, false);
        if (!value)
            return std::nullopt;
        at = *value;
        if (at >= end_of(text) || text_at(text, at) != kDictClose)
            return std::nullopt;
        return advance(at, WireU8::kWidth);
    }
    if (code == kVariant)
        return advance(at, WireU8::kWidth);
    if (!is_basic(code))
        return std::nullopt;
    return advance(at, WireU8::kWidth);
}

[[nodiscard]] std::optional<BufferOffset> type_extent(
    std::span<const char> text,
    BufferOffset at,
    bool allow_dict
) noexcept {
    return parse_complete(text, at, allow_dict);
}

[[nodiscard]] bool text_is_valid(DbusHeaderField::Code code, std::string_view text) noexcept {
    switch (code) {
        case DbusHeaderField::Code::Path:
            return object_path_valid(text);
        case DbusHeaderField::Code::Interface:
        case DbusHeaderField::Code::ErrorName:
            return names_valid(text) && !text.empty() && text.front() != ':';
        case DbusHeaderField::Code::Member:
            return member_valid(text);
        case DbusHeaderField::Code::Destination:
        case DbusHeaderField::Code::Sender:
            return names_valid(text);
        case DbusHeaderField::Code::Signature:
            return signature_valid(text, true);
        case DbusHeaderField::Code::ReplySerial:
        case DbusHeaderField::Code::kCount:
            return false;
    }
    return false;
}

[[nodiscard]] char field_signature(DbusHeaderField::Code code) noexcept {
    switch (code) {
        case DbusHeaderField::Code::Path:
            return kObjectPath;
        case DbusHeaderField::Code::ReplySerial:
            return kUint32;
        case DbusHeaderField::Code::Signature:
            return kSignature;
        case DbusHeaderField::Code::Interface:
        case DbusHeaderField::Code::Member:
        case DbusHeaderField::Code::ErrorName:
        case DbusHeaderField::Code::Destination:
        case DbusHeaderField::Code::Sender:
            return kString;
        case DbusHeaderField::Code::kCount:
            return kString;
    }
    return kString;
}

[[nodiscard]] Ex<BufferOffset> skip_complete(
    DbusReader& reader,
    std::string_view sig,
    BufferOffset at
);

[[nodiscard]] Ex<void> skip_basic(DbusReader& reader, char code) noexcept {
    switch (code) {
        case kByte: {
            auto v = reader.u8();
            if (!v)
                return std::unexpected(v.error());
            return {};
        }
        case kBoolean: {
            auto v = reader.boolean();
            if (!v)
                return std::unexpected(v.error());
            return {};
        }
        case kInt16: {
            auto v = reader.i16();
            if (!v)
                return std::unexpected(v.error());
            return {};
        }
        case kUint16: {
            auto v = reader.u16();
            if (!v)
                return std::unexpected(v.error());
            return {};
        }
        case kInt32: {
            auto v = reader.i32();
            if (!v)
                return std::unexpected(v.error());
            return {};
        }
        case kUint32: {
            auto v = reader.u32();
            if (!v)
                return std::unexpected(v.error());
            return {};
        }
        case kInt64: {
            auto v = reader.i64();
            if (!v)
                return std::unexpected(v.error());
            return {};
        }
        case kUint64: {
            auto v = reader.u64();
            if (!v)
                return std::unexpected(v.error());
            return {};
        }
        case kString: {
            auto v = reader.string();
            if (!v)
                return std::unexpected(v.error());
            return {};
        }
        case kObjectPath: {
            auto v = reader.object_path();
            if (!v)
                return std::unexpected(v.error());
            return {};
        }
        case kSignature: {
            auto v = reader.signature();
            if (!v)
                return std::unexpected(v.error());
            return {};
        }
        default:
            return std::unexpected(format_error());
    }
}

[[nodiscard]] Ex<BufferOffset> skip_array(
    DbusReader& reader,
    std::string_view sig,
    BufferOffset at
) noexcept {
    const std::span<const char> text = chars_of(sig);
    const BufferOffset element_begin = at;
    const auto element_end = type_extent(text, at, true);
    if (!element_end)
        return std::unexpected(format_error());
    const BufferOffset after_element = *element_end;
    auto length = reader.u32();
    if (!length)
        return std::unexpected(length.error());
    const std::span<const char> element = element_begin.up_to(after_element, text);
    reader.align_to(align_of(element.front()));
    const WireBytes count = as_bytes(*length);
    if (count > reader.remaining())
        return std::unexpected(format_error());
    const BufferOffset end = reader.position() + count;
    while (reader.position() < end) {
        const BufferOffset before = reader.position();
        auto stepped = skip_complete(reader, sig, element_begin);
        if (!stepped)
            return std::unexpected(stepped.error());
        if (reader.position() == before || reader.position() > end) {
            return std::unexpected(format_error());
        }
    }
    if (reader.position() != end)
        return std::unexpected(format_error());
    return after_element;
}

[[nodiscard]] Ex<BufferOffset> skip_complete(
    DbusReader& reader,
    std::string_view sig,
    BufferOffset at
) {
    const std::span<const char> text = chars_of(sig);
    if (at >= end_of(text))
        return std::unexpected(format_error());
    const char code = text_at(text, at);
    if (code == kArray) {
        return skip_array(reader, sig, advance(at, WireU8::kWidth));
    }
    if (code == kDictOpen) {
        at = advance(at, WireU8::kWidth);
        reader.align_to(WireU64::kWidth);
        if (at >= end_of(text) || !is_basic(text_at(text, at))) {
            return std::unexpected(format_error());
        }
        const auto key = skip_complete(reader, sig, at);
        if (!key)
            return std::unexpected(key.error());
        at = *key;
        if (at >= end_of(text))
            return std::unexpected(format_error());
        const auto value = skip_complete(reader, sig, at);
        if (!value)
            return std::unexpected(value.error());
        at = *value;
        if (at >= end_of(text) || text_at(text, at) != kDictClose) {
            return std::unexpected(format_error());
        }
        at = advance(at, WireU8::kWidth);
        reader.align_to(WireU64::kWidth);
        return at;
    }
    if (code == kVariant) {
        at = advance(at, WireU8::kWidth);
        auto inner = reader.signature();
        if (!inner)
            return std::unexpected(inner.error());
        const std::span<const char> nested_sig = chars_of(*inner);
        BufferOffset nested = BufferOffset::kStart;
        while (nested < end_of(nested_sig)) {
            const auto stepped = skip_complete(reader, *inner, nested);
            if (!stepped)
                return std::unexpected(stepped.error());
            nested = *stepped;
        }
        return at;
    }
    if (!is_basic(code))
        return std::unexpected(format_error());
    at = advance(at, WireU8::kWidth);
    reader.align_to(align_of(code));
    auto skipped = skip_basic(reader, code);
    if (!skipped)
        return std::unexpected(skipped.error());
    return at;
}

}

namespace detail {

bool object_path_valid(std::string_view path) noexcept {
    if (path.empty() || path.size() > DbusObjectPath::kMaxLength)
        return false;
    if (path.front() != '/')
        return false;
    if (path.size() == 1)
        return true;
    if (path.back() == '/')
        return false;
    for (std::size_t i = 1; i < path.size(); ++i) {
        if (path[i] == '/') {
            if (path[i - 1] == '/')
                return false;
            continue;
        }
        if (!is_element_char(path[i]))
            return false;
    }
    return true;
}

bool names_valid(std::string_view name) noexcept {
    if (name.empty() || name.size() > DbusBusName::kMaxLength)
        return false;
    const bool unique = name.front() == ':';
    std::size_t start = 0;
    if (unique) {
        start = 1;
        if (name.size() == 1)
            return false;
    }
    bool saw_dot = false;
    bool element_has_char = false;
    for (std::size_t i = start; i < name.size(); ++i) {
        const char c = name[i];
        if (c == '.') {
            if (!element_has_char)
                return false;
            saw_dot = true;
            element_has_char = false;
            continue;
        }
        if (!is_name_char(c))
            return false;
        if (!element_has_char && !unique && is_digit(c))
            return false;
        element_has_char = true;
    }
    return saw_dot && element_has_char;
}

bool member_valid(std::string_view member) noexcept {
    if (member.empty() || member.size() > DbusHeaderField::kTextMax)
        return false;
    if (is_digit(member.front()))
        return false;
    for (char c : member) {
        if (!is_word(c))
            return false;
    }
    return true;
}

bool signature_valid(std::string_view signature, bool allow_sequence) noexcept {
    if (signature.empty())
        return false;
    const std::span<const char> text = chars_of(signature);
    BufferOffset at = BufferOffset::kStart;
    do {
        const auto next = parse_complete(text, at, false);
        if (!next)
            return false;
        at = *next;
        if (!allow_sequence)
            break;
    } while (at < end_of(text));
    return at == end_of(text);
}

bool single_complete_type(std::string_view signature) noexcept {
    return signature_valid(signature, false);
}

bool element_type_valid(std::string_view signature) noexcept {
    if (signature.empty())
        return false;
    const auto at = parse_complete(chars_of(signature), BufferOffset::kStart, true);
    return at && *at == end_of(chars_of(signature));
}

}

DbusObjectPath::DbusObjectPath(std::string_view path) noexcept
    : length_(as_u16(WireBytes{path.size()})) {
    std::memcpy(text_.data(), path.data(), path.size());
}

Ex<DbusObjectPath> DbusObjectPath::parse(std::string_view path) noexcept {
    if (!detail::object_path_valid(path))
        return std::unexpected(format_error());
    return DbusObjectPath{path};
}

DbusBusName::DbusBusName(std::string_view name) noexcept : length_(as_u16(WireBytes{name.size()})) {
    std::memcpy(text_.data(), name.data(), name.size());
}

Ex<DbusBusName> DbusBusName::parse(std::string_view name) noexcept {
    if (!detail::names_valid(name))
        return std::unexpected(format_error());
    return DbusBusName{name};
}

DbusWriter::DbusWriter(std::span<std::byte> out) noexcept : out_(out) {}

bool DbusWriter::reserve(WireBytes bytes) noexcept {
    if (!ok_)
        return false;
    if (bytes > left_in(out_, at_)) {
        fail(Errc::short_write);
        return false;
    }
    return true;
}

void DbusWriter::fail(Errc code) noexcept {
    if (ok_) {
        ok_ = false;
        error_ = Error{code, ERR_SITE(), 0};
    }
}

void DbusWriter::align_to(WireBytes boundary) noexcept {
    if (boundary == WireBytes::kNone)
        return;
    const WireBytes pad = padding_to(at_, boundary);
    if (!reserve(pad))
        return;
    const std::span<std::byte> room = pad.head_of(at_.at(out_));
    std::memset(room.data(), 0, room.size());
    at_ = advance(at_, pad);
}

void DbusWriter::u8(WireU8 value) noexcept {
    if (!reserve(WireU8::kWidth))
        return;
    value.store_le(at_.at(out_));
    at_ = advance(at_, WireU8::kWidth);
}

void DbusWriter::boolean(bool value) noexcept {
    align_to(WireU32::kWidth);
    u32(value ? kWireTrue : kWireFalse);
}

void DbusWriter::i16(WireI16 value) noexcept {
    align_to(WireI16::kWidth);
    if (!reserve(WireI16::kWidth))
        return;
    value.store_le(at_.at(out_));
    at_ = advance(at_, WireI16::kWidth);
}

void DbusWriter::u16(WireU16 value) noexcept {
    align_to(WireU16::kWidth);
    if (!reserve(WireU16::kWidth))
        return;
    value.store_le(at_.at(out_));
    at_ = advance(at_, WireU16::kWidth);
}

void DbusWriter::i32(WireI32 value) noexcept {
    align_to(WireI32::kWidth);
    if (!reserve(WireI32::kWidth))
        return;
    value.store_le(at_.at(out_));
    at_ = advance(at_, WireI32::kWidth);
}

void DbusWriter::u32(WireU32 value) noexcept {
    align_to(WireU32::kWidth);
    if (!reserve(WireU32::kWidth))
        return;
    value.store_le(at_.at(out_));
    at_ = advance(at_, WireU32::kWidth);
}

void DbusWriter::i64(WireI64 value) noexcept {
    align_to(WireI64::kWidth);
    if (!reserve(WireI64::kWidth))
        return;
    value.store_le(at_.at(out_));
    at_ = advance(at_, WireI64::kWidth);
}

void DbusWriter::u64(WireU64 value) noexcept {
    align_to(WireU64::kWidth);
    if (!reserve(WireU64::kWidth))
        return;
    value.store_le(at_.at(out_));
    at_ = advance(at_, WireU64::kWidth);
}

bool DbusWriter::text(std::string_view value) noexcept {
    if (!reserve(WireBytes{value.size()} + WireU8::kWidth))
        return false;
    const std::span<std::byte> run = WireBytes{value.size()}.head_of(at_.at(out_));
    std::memcpy(run.data(), value.data(), value.size());
    at_ = advance(at_, WireBytes{value.size()});

    at_.at(out_).front() = std::byte{0};
    at_ = advance(at_, WireU8::kWidth);
    return true;
}

void DbusWriter::string(std::string_view value) noexcept {
    align_to(WireU32::kWidth);
    if (WireBytes{value.size()} > WireU32::kMaxCount) {
        fail(Errc::bad_format);
        return;
    }
    u32(as_u32(WireBytes{value.size()}));
    (void)text(value);
}

void DbusWriter::object_path(const DbusObjectPath& path) noexcept {
    string(path.view());
}

void DbusWriter::bus_name(const DbusBusName& name) noexcept {
    string(name.view());
}

void DbusWriter::signature(std::string_view value) noexcept {
    if (WireBytes{value.size()} > WireU8::kMaxCount || !detail::signature_valid(value, true)) {
        fail(Errc::bad_format);
        return;
    }
    u8(as_u8(WireBytes{value.size()}));
    (void)text(value);
}

Ex<BufferOffset> DbusWriter::begin_array(std::string_view element_signature) noexcept {
    if (!detail::element_type_valid(element_signature)) {
        fail(Errc::bad_format);
        return std::unexpected(error_);
    }
    if (array_depth_ == kMaxArrayDepth) {
        fail(Errc::bad_format);
        return std::unexpected(error_);
    }
    align_to(WireU32::kWidth);
    const BufferOffset length_offset = at_;
    u32(WireU32{0});
    align_to(align_of(element_signature.front()));
    if (!ok_)
        return std::unexpected(error_);
    nth(std::span<ArrayFrame>{arrays_}, array_depth_) = ArrayFrame{length_offset, at_};
    array_depth_ = array_depth_.next();
    return nth(std::span<const ArrayFrame>{arrays_}, array_depth_.previous()).data_offset;
}

void DbusWriter::end_array(BufferOffset token) noexcept {
    if (!ok_)
        return;
    if (array_depth_ == WireBytes::kNone) {
        fail(Errc::bad_format);
        return;
    }
    array_depth_ = array_depth_.previous();
    const ArrayFrame frame = nth(std::span<const ArrayFrame>{arrays_}, array_depth_);
    if (token != frame.data_offset || at_ < frame.data_offset) {
        fail(Errc::bad_format);
        return;
    }
    as_u32(at_.since(frame.data_offset)).store_le(frame.length_offset.at(out_));
}

Ex<void> DbusWriter::begin_variant(std::string_view value_signature) noexcept {
    if (!detail::single_complete_type(value_signature)) {
        fail(Errc::bad_format);
        return std::unexpected(error_);
    }
    signature(value_signature);
    if (!ok_)
        return std::unexpected(error_);
    return {};
}

void DbusWriter::begin_dict_entry() noexcept {
    align_to(WireU64::kWidth);
}

void DbusWriter::end_dict_entry() noexcept {}

DbusReader::DbusReader(std::span<const std::byte> bytes, bool big_endian) noexcept
    : bytes_(bytes),
      big_(big_endian) {}

bool DbusReader::need(WireBytes bytes) noexcept {
    if (!ok_)
        return false;
    if (bytes > remaining()) {
        ok_ = false;
        error_ = Error{Errc::short_read, ERR_SITE(), 0};
        return false;
    }
    return true;
}

Ex<void> DbusReader::fail(Errc code) noexcept {
    ok_ = false;
    error_ = Error{code, ERR_SITE(), 0};
    return std::unexpected(error_);
}

void DbusReader::align_to(WireBytes boundary) noexcept {
    if (boundary == WireBytes::kNone)
        return;
    const WireBytes pad = padding_to(at_, boundary);
    if (!need(pad))
        return;
    at_ = advance(at_, pad);
}

Ex<WireU64> DbusReader::read_raw(WireBytes bytes) noexcept {
    if (!need(bytes))
        return std::unexpected(error_);
    const std::span<const std::byte> word = bytes.head_of(at_.at(bytes_));
    const WireU64 value = big_ ? WireU64::of_be(word, bytes) : WireU64::of_le(word, bytes);
    at_ = advance(at_, bytes);
    return value;
}

Ex<WireU8> DbusReader::u8() noexcept {
    if (!need(WireU8::kWidth))
        return std::unexpected(error_);
    const WireU8 value = WireU8::of_le(at_.at(bytes_));
    at_ = advance(at_, WireU8::kWidth);
    return value;
}

Ex<bool> DbusReader::boolean() noexcept {
    align_to(WireU32::kWidth);
    auto value = read_raw(WireU32::kWidth);
    if (!value)
        return std::unexpected(value.error());
    const WireU32 word = value->low_u32();
    if (word != kWireTrue && word != kWireFalse)
        return std::unexpected(format_error());
    return word == kWireTrue;
}

Ex<WireI16> DbusReader::i16() noexcept {
    align_to(WireI16::kWidth);
    auto value = read_raw(WireI16::kWidth);
    if (!value)
        return std::unexpected(value.error());
    return as_signed(value->low_u16());
}

Ex<WireU16> DbusReader::u16() noexcept {
    align_to(WireU16::kWidth);
    auto value = read_raw(WireU16::kWidth);
    if (!value)
        return std::unexpected(value.error());
    return value->low_u16();
}

Ex<WireI32> DbusReader::i32() noexcept {
    align_to(WireI32::kWidth);
    auto value = read_raw(WireI32::kWidth);
    if (!value)
        return std::unexpected(value.error());
    return as_signed(value->low_u32());
}

Ex<WireU32> DbusReader::u32() noexcept {
    align_to(WireU32::kWidth);
    auto value = read_raw(WireU32::kWidth);
    if (!value)
        return std::unexpected(value.error());
    return value->low_u32();
}

Ex<WireI64> DbusReader::i64() noexcept {
    align_to(WireI64::kWidth);
    auto value = read_raw(WireI64::kWidth);
    if (!value)
        return std::unexpected(value.error());
    return as_signed(*value);
}

Ex<WireU64> DbusReader::u64() noexcept {
    align_to(WireU64::kWidth);
    return read_raw(WireU64::kWidth);
}

Ex<std::string_view> DbusReader::text(WireBytes length) noexcept {
    if (!need(length + WireU8::kWidth))
        return std::unexpected(error_);
    const std::span<const std::byte> run = at_.at(bytes_);
    const std::span<const std::byte> body = length.head_of(run);
    const std::span<const std::byte> terminator = length.tail_of(run);
    if (terminator.front() != std::byte{0}) {
        (void)fail(Errc::bad_format);
        return std::unexpected(error_);
    }
    at_ = advance(at_, length + WireU8::kWidth);
    return infra::as_text(body);
}

Ex<std::string_view> DbusReader::string() noexcept {
    align_to(WireU32::kWidth);
    auto length = u32();
    if (!length)
        return std::unexpected(length.error());
    return text(as_bytes(*length));
}

Ex<DbusObjectPath> DbusReader::object_path() noexcept {
    auto text_value = string();
    if (!text_value)
        return std::unexpected(text_value.error());
    auto parsed = DbusObjectPath::parse(*text_value);
    if (!parsed)
        return std::unexpected(parsed.error());
    return *parsed;
}

Ex<DbusBusName> DbusReader::bus_name() noexcept {
    auto text_value = string();
    if (!text_value)
        return std::unexpected(text_value.error());
    auto parsed = DbusBusName::parse(*text_value);
    if (!parsed)
        return std::unexpected(parsed.error());
    return *parsed;
}

Ex<std::string_view> DbusReader::signature() noexcept {
    align_to(WireU8::kWidth);
    auto length = u8();
    if (!length)
        return std::unexpected(length.error());
    auto value = text(as_bytes(*length));
    if (!value)
        return std::unexpected(value.error());
    if (!detail::signature_valid(*value, true)) {
        (void)fail(Errc::bad_format);
        return std::unexpected(error_);
    }
    return *value;
}

Ex<void> DbusReader::skip_value(std::string_view signature) noexcept {
    if (!detail::single_complete_type(signature))
        return fail(Errc::bad_format);
    auto skipped = skip_complete(*this, signature, BufferOffset::kStart);
    if (!skipped)
        return std::unexpected(skipped.error());
    return {};
}

Ex<void> DbusReader::skip(std::string_view signature) noexcept {
    if (!detail::signature_valid(signature, true))
        return fail(Errc::bad_format);
    const std::span<const char> text_value = chars_of(signature);
    BufferOffset at = BufferOffset::kStart;
    while (at < end_of(text_value)) {
        auto stepped = skip_complete(*this, signature, at);
        if (!stepped)
            return std::unexpected(stepped.error());
        at = *stepped;
    }
    return {};
}

Ex<void> DbusHeader::put_field(const DbusHeaderField& field) noexcept {
    for (const std::size_t i : field_count.indices()) {
        if (fields[i].code == field.code) {
            fields[i] = field;
            return {};
        }
    }
    if (field_count >= kFieldCap)
        return std::unexpected(format_error());
    nth(std::span<DbusHeaderField>{fields}, as_count(field_count)) = field;
    field_count = field_count.next();
    return {};
}

Ex<void> DbusHeader::add_field(DbusHeaderField::Code code, std::string_view text_value) noexcept {
    if (code == DbusHeaderField::Code::ReplySerial || code == DbusHeaderField::Code::kCount) {
        return std::unexpected(format_error());
    }
    if (text_value.size() > DbusHeaderField::kTextMax)
        return std::unexpected(format_error());
    if (!text_is_valid(code, text_value))
        return std::unexpected(format_error());
    DbusHeaderField field{};
    field.code = code;
    field.length = as_u16(WireBytes{text_value.size()});
    std::memcpy(field.text.data(), text_value.data(), text_value.size());
    return put_field(field);
}

Ex<void> DbusHeader::add_reply_serial(DbusSerial reply_serial) noexcept {
    if (reply_serial.value() == 0)
        return std::unexpected(format_error());
    DbusHeaderField field{};
    field.code = DbusHeaderField::Code::ReplySerial;
    field.number = as_wire(reply_serial);
    return put_field(field);
}

const DbusHeaderField* DbusHeader::find(DbusHeaderField::Code code) const noexcept {
    for (const std::size_t i : field_count.indices()) {
        if (fields[i].code == code)
            return &fields[i];
    }
    return nullptr;
}

std::string_view DbusHeader::path() const noexcept {
    const DbusHeaderField* field = find(DbusHeaderField::Code::Path);
    return field == nullptr ? std::string_view{} : field->view();
}

std::string_view DbusHeader::interface_name() const noexcept {
    const DbusHeaderField* field = find(DbusHeaderField::Code::Interface);
    return field == nullptr ? std::string_view{} : field->view();
}

std::string_view DbusHeader::member() const noexcept {
    const DbusHeaderField* field = find(DbusHeaderField::Code::Member);
    return field == nullptr ? std::string_view{} : field->view();
}

std::string_view DbusHeader::error_name() const noexcept {
    const DbusHeaderField* field = find(DbusHeaderField::Code::ErrorName);
    return field == nullptr ? std::string_view{} : field->view();
}

std::string_view DbusHeader::destination() const noexcept {
    const DbusHeaderField* field = find(DbusHeaderField::Code::Destination);
    return field == nullptr ? std::string_view{} : field->view();
}

std::string_view DbusHeader::sender() const noexcept {
    const DbusHeaderField* field = find(DbusHeaderField::Code::Sender);
    return field == nullptr ? std::string_view{} : field->view();
}

std::string_view DbusHeader::signature() const noexcept {
    const DbusHeaderField* field = find(DbusHeaderField::Code::Signature);
    return field == nullptr ? std::string_view{} : field->view();
}

DbusSerial DbusHeader::reply_serial() const noexcept {
    const DbusHeaderField* field = find(DbusHeaderField::Code::ReplySerial);
    return field == nullptr ? DbusSerial{} : as_serial(field->number);
}

Ex<WireBytes> DbusHeader::encode(std::span<std::byte> out) const noexcept {
    if (serial.value() == 0 || field_count > kFieldCap) {
        return std::unexpected(format_error());
    }
    if (as_bytes(body_length) > kMaxMessage)
        return std::unexpected(format_error());
    if (type != Type::MethodCall
        && type != Type::MethodReturn
        && type != Type::Error
        && type != Type::Signal) {
        return std::unexpected(format_error());
    }

    DbusWriter writer{out};
    writer.u8(kLittleEndianMarker);
    writer.u8(wire_type(type));
    writer.u8(flags);
    writer.u8(kProtocolVersion);
    writer.u32(body_length);
    writer.u32(as_wire(serial));
    const BufferOffset length_offset = as_offset(writer.offset());
    writer.u32(WireU32{0});

    for (const std::size_t i : field_count.indices()) {
        const DbusHeaderField& field = fields[i];
        writer.align_to(WireU64::kWidth);
        writer.u8(wire_code(field.code));
        const char sig = field_signature(field.code);
        writer.signature(std::string_view{&sig, 1});
        writer.align_to(align_of(sig));
        if (field.code == DbusHeaderField::Code::ReplySerial) {
            writer.u32(field.number);
        } else if (field.code == DbusHeaderField::Code::Signature) {
            writer.signature(field.view());
        } else {
            writer.string(field.view());
        }
    }
    if (!writer.ok())
        return std::unexpected(writer.error());
    const WireBytes array_length = as_offset(writer.offset()).since(length_offset)
        - WireU32::kWidth;
    writer.align_to(WireU64::kWidth);
    if (!writer.ok())
        return std::unexpected(writer.error());
    as_u32(array_length).store_le(length_offset.at(out));
    return writer.offset();
}

Ex<DbusHeader> DbusHeader::decode(std::span<const std::byte> bytes) noexcept {
    if (WireBytes{bytes.size()} < kFixedSize)
        return std::unexpected(short_read_error());
    const WireU8 marker = WireU8::of_le(bytes);
    bool big = false;
    if (marker == kLittleEndianMarker) {
        big = false;
    } else if (marker == kBigEndianMarker) {
        big = true;
    } else {
        return std::unexpected(format_error());
    }
    const Type message_type = DbusHeader::header_type(WireU8::of_le(WireU8::kWidth.tail_of(bytes)));
    if (message_type != Type::MethodCall
        && message_type != Type::MethodReturn
        && message_type != Type::Error
        && message_type != Type::Signal) {
        return std::unexpected(format_error());
    }
    if (WireU8::of_le(kVersionAt.at(bytes)) != kProtocolVersion) {
        return std::unexpected(format_error());
    }
    const WireBytes array_length = as_bytes(
        load_u32(bytes, as_offset(kFixedSize - WireU32::kWidth), big)
    );
    if (array_length > kMaxMessage)
        return std::unexpected(format_error());
    if (array_length > WireBytes{bytes.size()} - kFixedSize) {
        return std::unexpected(short_read_error());
    }
    DbusHeader header{};
    header.type = message_type;
    header.flags = WireU8::of_le(kFlagsAt.at(bytes));
    header.serial = as_serial(load_u32(bytes, kSerialAt, big));
    if (header.serial.value() == 0)
        return std::unexpected(format_error());
    header.body_length = load_u32(bytes, kBodyLengthAt, big);
    if (as_bytes(header.body_length) > kMaxMessage)
        return std::unexpected(format_error());

    const std::span<const std::byte> array = array_length.head_of(kFixedSize.tail_of(bytes));
    DbusReader reader{array, big};
    while (!reader.at_end()) {
        reader.align_to(WireU64::kWidth);
        if (reader.at_end())
            break;
        auto code_value = reader.u8();
        if (!code_value)
            return std::unexpected(code_value.error());
        if (!field_code_known(*code_value))
            return std::unexpected(format_error());
        const DbusHeaderField::Code code = DbusHeaderField::field_code(*code_value);
        auto sig = reader.signature();
        if (!sig)
            return std::unexpected(sig.error());
        if (sig->size() != 1 || sig->front() != field_signature(code)) {
            return std::unexpected(format_error());
        }
        reader.align_to(align_of(sig->front()));
        DbusHeaderField field{};
        field.code = code;
        if (code == DbusHeaderField::Code::ReplySerial) {
            auto number = reader.u32();
            if (!number)
                return std::unexpected(number.error());
            if (*number == WireU32{0})
                return std::unexpected(format_error());
            field.number = *number;
        } else {
            auto text_value = code == DbusHeaderField::Code::Signature ? reader.signature()
                                                                       : reader.string();
            if (!text_value)
                return std::unexpected(text_value.error());
            if (text_value->size() > DbusHeaderField::kTextMax) {
                return std::unexpected(format_error());
            }
            if (!text_is_valid(code, *text_value))
                return std::unexpected(format_error());
            field.length = as_u16(WireBytes{text_value->size()});
            std::memcpy(field.text.data(), text_value->data(), text_value->size());
        }
        auto stored = header.put_field(field);
        if (!stored)
            return std::unexpected(stored.error());
    }
    if (reader.offset() != array_length)
        return std::unexpected(format_error());
    return header;
}

}
