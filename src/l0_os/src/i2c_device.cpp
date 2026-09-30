// SPDX-License-Identifier: GPL-3.0-or-later
#include "os/i2c_device.h"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>

#include <cerrno>
#include <cstdio>

namespace mister::os {

namespace {

constexpr std::size_t kDevPathMax = 32;

Ex<UniqueFd> open_i2c_node(unsigned bus, std::uint8_t addr) {
    char path[kDevPathMax];
    std::snprintf(path, sizeof path, "/dev/i2c-%u", bus);

    const int fd = ::open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(
            Error{Errc::uio_open, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    UniqueFd owned(fd);

    if (::ioctl(owned.get(), I2C_SLAVE, static_cast<int>(addr)) < 0) {
        return std::unexpected(
            Error{Errc::uio_open, ERR_SITE(), static_cast<std::uint32_t>(errno)});
    }
    return owned;
}

Ex<void> smbus_ioctl(int fd, std::uint8_t read_write, std::uint8_t command, int type,
                     i2c_smbus_data* data, std::uint16_t site) {
    i2c_smbus_ioctl_data args{};
    args.read_write = read_write;
    args.command = command;
    args.size = static_cast<std::uint32_t>(type);
    args.data = data;
    if (::ioctl(fd, I2C_SMBUS, &args) < 0) {
        return std::unexpected(Error{Errc::io, site, static_cast<std::uint32_t>(errno)});
    }
    return {};
}

}  // namespace

Ex<I2cDevice> I2cDevice::open(unsigned bus, std::uint8_t addr) {
    auto fd = open_i2c_node(bus, addr);
    if (!fd) return std::unexpected(fd.error());
    I2cDevice dev;
    dev.fd_ = std::move(*fd);
    return dev;
}

Ex<std::uint8_t> I2cDevice::read_reg(std::uint8_t reg) {
    i2c_smbus_data data{};
    if (auto r = smbus_ioctl(fd_.get(), static_cast<std::uint8_t>(I2C_SMBUS_READ), reg,
                             I2C_SMBUS_BYTE_DATA, &data, ERR_SITE());
        !r) {
        return std::unexpected(r.error());
    }
    return static_cast<std::uint8_t>(data.byte);
}

Ex<std::uint8_t> I2cDevice::read_byte() {
    i2c_smbus_data data{};
    if (auto r = smbus_ioctl(fd_.get(), static_cast<std::uint8_t>(I2C_SMBUS_READ), 0,
                             I2C_SMBUS_BYTE, &data, ERR_SITE());
        !r) {
        return std::unexpected(r.error());
    }
    return static_cast<std::uint8_t>(data.byte);
}

Ex<std::uint16_t> I2cDevice::read_word(std::uint8_t reg) {
    i2c_smbus_data data{};
    if (auto r = smbus_ioctl(fd_.get(), static_cast<std::uint8_t>(I2C_SMBUS_READ), reg,
                             I2C_SMBUS_WORD_DATA, &data, ERR_SITE());
        !r) {
        return std::unexpected(r.error());
    }
    return static_cast<std::uint16_t>(data.word);
}

Ex<void> I2cDevice::write_reg(std::uint8_t reg, std::uint8_t value) {
    i2c_smbus_data data{};
    data.byte = value;
    return smbus_ioctl(fd_.get(), static_cast<std::uint8_t>(I2C_SMBUS_WRITE), reg,
                       I2C_SMBUS_BYTE_DATA, &data, ERR_SITE());
}

Ex<void> I2cDevice::read_block(std::uint8_t reg, std::span<std::uint8_t> out) {
    constexpr auto kBlockMax = static_cast<std::size_t>(I2C_SMBUS_BLOCK_MAX);
    if (out.empty() || out.size() > kBlockMax) {
        return std::unexpected(
            Error{Errc::bad_format, ERR_SITE(), static_cast<std::uint32_t>(out.size())});
    }

    i2c_smbus_data data{};
    data.block[0] = static_cast<std::uint8_t>(out.size());

    const int type =
        out.size() == kBlockMax ? I2C_SMBUS_I2C_BLOCK_BROKEN : I2C_SMBUS_I2C_BLOCK_DATA;
    if (auto r = smbus_ioctl(fd_.get(), static_cast<std::uint8_t>(I2C_SMBUS_READ), reg, type, &data,
                             ERR_SITE());
        !r) {
        return std::unexpected(r.error());
    }

    if (static_cast<std::size_t>(data.block[0]) != out.size()) {
        return std::unexpected(
            Error{Errc::short_read, ERR_SITE(), static_cast<std::uint32_t>(data.block[0])});
    }
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = data.block[i + 1];
    return {};
}

}  // namespace mister::os
