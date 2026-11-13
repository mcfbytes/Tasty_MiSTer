// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

#include "infra/error.h"
#include "infra/seat.h"
#include "os/dbus_serial.h"
#include "os/types.h"

namespace mister::os {

struct DbusHeader;

struct DbusHeaderField {
    TASTY_SEAT_EXEMPT(component);

    enum class Code : std::uint8_t {
        Path = 1,
        Interface = 2,
        Member = 3,
        ErrorName = 4,
        ReplySerial = 5,
        Destination = 6,
        Sender = 7,
        Signature = 8,
        kCount = 9,
    };

    enum : std::size_t { kTextMax = 255 };

    Code code = Code::Path;

    WireU8 reserved{};
    WireU16 length{};
    WireU32 number{};
    std::array<char, kTextMax + 1> text{};

    [[nodiscard]] std::string_view view() const noexcept {
        return length.text_of(std::span<const char>{text});
    }

    [[nodiscard]] static constexpr Code field_code(WireU8 byte) noexcept {
        return static_cast<Code>(byte.v);
    }
};

[[nodiscard]] constexpr WireU8 wire_code(DbusHeaderField::Code code) noexcept {
    return WireU8{static_cast<std::uint8_t>(code)};
}

[[nodiscard]] constexpr bool field_code_known(WireU8 code) noexcept {
    switch (DbusHeaderField::field_code(code)) {
        case DbusHeaderField::Code::Path:
        case DbusHeaderField::Code::Interface:
        case DbusHeaderField::Code::Member:
        case DbusHeaderField::Code::ErrorName:
        case DbusHeaderField::Code::ReplySerial:
        case DbusHeaderField::Code::Destination:
        case DbusHeaderField::Code::Sender:
        case DbusHeaderField::Code::Signature:
            return true;
        case DbusHeaderField::Code::kCount:
            return false;
    }
    return false;
}

static_assert(std::is_trivially_copyable_v<DbusHeaderField>);
static_assert(
    sizeof(DbusHeaderField)
    == 2 * sizeof(std::uint8_t)
        + sizeof(std::uint16_t)
        + sizeof(std::uint32_t)
        + DbusHeaderField::kTextMax
        + 1
);

struct DbusHeader {
    TASTY_SEAT_EXEMPT(component);

    enum class Type : std::uint8_t {
        MethodCall = 1,
        MethodReturn = 2,
        Error = 3,
        Signal = 4,
        kCount = 5,
    };

    static constexpr WireBytes kFixedSize{16};

    enum : std::size_t { kMaxFields = 8 };

    static constexpr WireBytes kMaxFieldCount{kMaxFields};
    static constexpr WireBytes kMessageAlign{8};
    static constexpr WireBytes kMaxMessage{1u << 27};
    static constexpr WireU8 kProtocolVersion{1};

    static constexpr WireU8 kFieldCap{static_cast<std::uint8_t>(kMaxFields)};

    Type type = Type::MethodCall;
    WireU8 flags{};
    DbusSerial serial{};
    WireU32 body_length{};
    WireU8 field_count{};
    std::array<DbusHeaderField, kMaxFields> fields{};

    [[nodiscard]] Ex<void> add_field(DbusHeaderField::Code code, std::string_view text) noexcept;
    [[nodiscard]] Ex<void> add_reply_serial(DbusSerial reply_serial) noexcept;
    [[nodiscard]] const DbusHeaderField* find(DbusHeaderField::Code code) const noexcept;

    [[nodiscard]] std::string_view path() const noexcept;
    [[nodiscard]] std::string_view interface_name() const noexcept;
    [[nodiscard]] std::string_view member() const noexcept;
    [[nodiscard]] std::string_view error_name() const noexcept;
    [[nodiscard]] std::string_view destination() const noexcept;
    [[nodiscard]] std::string_view sender() const noexcept;
    [[nodiscard]] std::string_view signature() const noexcept;
    [[nodiscard]] DbusSerial reply_serial() const noexcept;

    [[nodiscard]] static constexpr WireBytes message_size(
        WireBytes header_size,
        WireBytes body_length
    ) noexcept {
        return header_size + body_length;
    }

    [[nodiscard]] Ex<WireBytes> encode(std::span<std::byte> out) const noexcept;
    [[nodiscard]] static Ex<DbusHeader> decode(std::span<const std::byte> bytes) noexcept;

    [[nodiscard]] static constexpr Type header_type(WireU8 byte) noexcept {
        return static_cast<Type>(byte.v);
    }

private:
    [[nodiscard]] Ex<void> put_field(const DbusHeaderField& field) noexcept;
};

static_assert(std::is_trivially_copyable_v<DbusHeader>);

static_assert(
    DbusHeader::kFixedSize
    == WireBytes{sizeof(DbusHeader) - DbusHeader::kMaxFields * sizeof(DbusHeaderField)}
);

[[nodiscard]] constexpr WireU8 wire_type(DbusHeader::Type type) noexcept {
    return WireU8{static_cast<std::uint8_t>(type)};
}

}
