// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
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

class DbusWriter {
    TASTY_SEAT_EXEMPT(component);

public:
    explicit DbusWriter(std::span<std::byte> out) noexcept;

    [[nodiscard]] bool ok() const noexcept { return ok_; }

    [[nodiscard]] Error error() const noexcept { return error_; }

    [[nodiscard]] WireBytes offset() const noexcept { return at_.since(BufferOffset::kStart); }

    [[nodiscard]] std::span<const std::byte> written() const noexcept {
        return offset().head_of(out_);
    }

    void align_to(WireBytes boundary) noexcept;
    void u8(WireU8 value) noexcept;
    void boolean(bool value) noexcept;
    void i16(WireI16 value) noexcept;
    void u16(WireU16 value) noexcept;
    void i32(WireI32 value) noexcept;
    void u32(WireU32 value) noexcept;
    void i64(WireI64 value) noexcept;
    void u64(WireU64 value) noexcept;
    void string(std::string_view value) noexcept;
    void object_path(const DbusObjectPath& path) noexcept;
    void bus_name(const DbusBusName& name) noexcept;
    void signature(std::string_view value) noexcept;

    [[nodiscard]] Ex<BufferOffset> begin_array(std::string_view element_signature) noexcept;
    void end_array(BufferOffset token) noexcept;

    [[nodiscard]] Ex<void> begin_variant(std::string_view value_signature) noexcept;

    void begin_dict_entry() noexcept;
    void end_dict_entry() noexcept;

private:
    void fail(Errc code) noexcept;
    [[nodiscard]] bool reserve(WireBytes bytes) noexcept;

    [[nodiscard]] bool text(std::string_view value) noexcept;

    struct ArrayFrame {

        BufferOffset length_offset{};
        BufferOffset data_offset{};
    };

    enum : std::size_t { kArrayDepth = 8 };

    static constexpr WireBytes kMaxArrayDepth{kArrayDepth};

    std::span<std::byte> out_;
    BufferOffset at_{};
    bool ok_ = true;
    Error error_{};
    std::array<ArrayFrame, kArrayDepth> arrays_{};
    WireBytes array_depth_{};
};

}
