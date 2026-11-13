// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "infra/error.h"
#include "infra/seat.h"
#include "os/dbus_bus_name.h"
#include "os/dbus_object_path.h"
#include "os/types.h"

namespace mister::os {

class DbusReader {
    TASTY_SEAT_EXEMPT(component);

public:
    DbusReader(std::span<const std::byte> bytes, bool big_endian) noexcept;

    [[nodiscard]] bool ok() const noexcept { return ok_; }

    [[nodiscard]] bool big_endian() const noexcept { return big_; }

    [[nodiscard]] WireBytes offset() const noexcept { return at_.since(BufferOffset::kStart); }

    [[nodiscard]] BufferOffset position() const noexcept { return at_; }

    [[nodiscard]] WireBytes remaining() const noexcept { return left_in(bytes_, at_); }

    [[nodiscard]] bool at_end() const noexcept { return at_ == end_of(bytes_); }

    void align_to(WireBytes boundary) noexcept;
    [[nodiscard]] Ex<WireU8> u8() noexcept;
    [[nodiscard]] Ex<bool> boolean() noexcept;
    [[nodiscard]] Ex<WireI16> i16() noexcept;
    [[nodiscard]] Ex<WireU16> u16() noexcept;
    [[nodiscard]] Ex<WireI32> i32() noexcept;
    [[nodiscard]] Ex<WireU32> u32() noexcept;
    [[nodiscard]] Ex<WireI64> i64() noexcept;
    [[nodiscard]] Ex<WireU64> u64() noexcept;
    [[nodiscard]] Ex<std::string_view> string() noexcept;
    [[nodiscard]] Ex<DbusObjectPath> object_path() noexcept;
    [[nodiscard]] Ex<DbusBusName> bus_name() noexcept;

    [[nodiscard]] Ex<std::string_view> signature() noexcept;

    [[nodiscard]] Ex<void> skip_value(std::string_view signature) noexcept;
    [[nodiscard]] Ex<void> skip(std::string_view signature) noexcept;

private:
    [[nodiscard]] bool need(WireBytes bytes) noexcept;
    [[nodiscard]] Ex<void> fail(Errc code) noexcept;

    [[nodiscard]] Ex<WireU64> read_raw(WireBytes bytes) noexcept;

    [[nodiscard]] Ex<std::string_view> text(WireBytes length) noexcept;

    std::span<const std::byte> bytes_;
    BufferOffset at_{};
    bool big_ = false;
    bool ok_ = true;
    Error error_{};
};

}
