// bno055_base.cpp Minimal driver for Bosch BNO055 nine degree of
// freedom inertial measurement unit module with sensor fusion.

// The MIT License (MIT)
//
// Copyright (c) 2017 Radomir Dopieralski for Adafruit Industries.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// This is a port of the Adafruit CircuitPython driver to MicroPython, with
// modified/enhanced functionality.

// Original Author: Radomir Dopieralski
// Ported to MicroPython and extended by Peter Hinch
// This port copyright (c) Peter Hinch 2019
// Ported from MicroPython (bno055_base.py) to C++ for the Raspberry Pi Pico SDK.

#include "bno055_base.h"

#include <algorithm>
#include "pico/stdlib.h"

namespace {

constexpr uint8_t CHIP_ID = 0xA0;

constexpr uint8_t CONFIG_MODE = 0;
constexpr uint8_t NDOF_MODE = 0x0C;

constexpr uint8_t POWER_NORMAL = 0x00;
constexpr uint8_t POWER_LOW = 0x01;
constexpr uint8_t POWER_SUSPEND = 0x02;

constexpr uint8_t MODE_REGISTER = 0x3D;
constexpr uint8_t PAGE_REGISTER = 0x07;
constexpr uint8_t CALIBRATION_REGISTER = 0x35;
constexpr uint8_t TRIGGER_REGISTER = 0x3F;
constexpr uint8_t POWER_REGISTER = 0x3E;
constexpr uint8_t ID_REGISTER = 0x00;

}  // namespace

BNO055_BASE::BNO055_BASE(i2c_inst_t *i2c, uint8_t address, bool crystal)
    : BNO055_BASE(i2c, address, crystal, DeferInit{}) {
    init();
}

BNO055_BASE::BNO055_BASE(i2c_inst_t *i2c, uint8_t address, bool crystal, DeferInit)
    : address(address), crystal(crystal), _i2c(i2c), _mode(CONFIG_MODE) {}

void BNO055_BASE::init() {
    uint8_t chip_id;
    if (!_readn(&chip_id, 1, ID_REGISTER)) {
        panic("No BNO055 chip detected.");
    }
    if (chip_id != CHIP_ID) {
        panic("bad chip id (%x != %x)", chip_id, CHIP_ID);
    }
    reset();
}

void BNO055_BASE::reset() {
    mode(CONFIG_MODE);
    // An I2C error here is expected because the chip is resetting
    _write(TRIGGER_REGISTER, 0x20);
    // wait for the chip to reset (650 ms typ.)
    sleep_ms(700);
    _write(POWER_REGISTER, POWER_NORMAL);
    _write(PAGE_REGISTER, 0x00);
    _write(TRIGGER_REGISTER, crystal ? 0x80 : 0);
    sleep_ms(crystal ? 500 : 10);  // Crystal osc seems to take time to start.
    orient();  // Subclass
    mode(NDOF_MODE);
}

// Reads count little endian signed 16 bit values and scales them
void BNO055_BASE::scaled_tuple(uint8_t addr, double scale, double *out, int count) {
    uint8_t buf[8];
    _readn(buf, count * 2, addr);
    for (int i = 0; i < count; i++) {
        int16_t b = (int16_t)(buf[2 * i] | (buf[2 * i + 1] << 8));
        out[i] = b * scale;
    }
}

Tuple3 BNO055_BASE::mag() {
    Tuple3 t;
    scaled_tuple(0x0E, 1.0 / 16, t.data(), 3);
    return t;
}

Tuple3 BNO055_BASE::accel() {
    Tuple3 t;
    scaled_tuple(0x08, 1.0 / 100, t.data(), 3);
    return t;
}

Tuple3 BNO055_BASE::lin_acc() {
    Tuple3 t;
    scaled_tuple(0x28, 1.0 / 100, t.data(), 3);
    return t;
}

Tuple3 BNO055_BASE::gravity() {
    Tuple3 t;
    scaled_tuple(0x2E, 1.0 / 100, t.data(), 3);
    return t;
}

Tuple3 BNO055_BASE::gyro() {
    Tuple3 t;
    scaled_tuple(0x14, 1.0 / 16, t.data(), 3);
    return t;
}

Tuple3 BNO055_BASE::euler() {
    Tuple3 t;
    scaled_tuple(0x1A, 1.0 / 16, t.data(), 3);
    return t;
}

Tuple4 BNO055_BASE::quaternion() {
    Tuple4 t;
    scaled_tuple(0x20, 1.0 / (1 << 14), t.data(), 4);
    return t;
}

int BNO055_BASE::temperature() {
    int t = _read(0x34);  // Celcius signed (corrected from Adafruit)
    return t < 128 ? t : t - 256;
}

// Return [sys, gyro, accel, mag] calibration data.
std::array<uint8_t, 4> BNO055_BASE::cal_status() {
    std::array<uint8_t, 4> s;
    uint8_t cdata = _read(CALIBRATION_REGISTER);
    s[0] = (cdata >> 6) & 0x03;  // sys
    s[1] = (cdata >> 4) & 0x03;  // gyro
    s[2] = (cdata >> 2) & 0x03;  // accel
    s[3] = cdata & 0x03;  // mag
    return s;
}

bool BNO055_BASE::calibrated() {
    std::array<uint8_t, 4> s = cal_status();
    // https://learn.adafruit.com/adafruit-bno055-absolute-orientation-sensor/device-calibration
    return std::min({s[1], s[2], s[3]}) == 3 && s[0] > 0;
}

std::array<uint8_t, 22> BNO055_BASE::sensor_offsets() {
    int lastMode = _mode;

    mode(CONFIG_MODE);
    std::array<uint8_t, 22> offsets;
    _readn(offsets.data(), 22, ACCEL_OFFSET_X_LSB_ADDR);
    mode(lastMode);

    return offsets;
}

void BNO055_BASE::set_offsets(const uint8_t *buf) {
    int lastMode = _mode;
    mode(CONFIG_MODE);

    sleep_ms(25);

    // Note: Configuration will take place only when user writes to the last
    // byte of each config data pair (ex. ACCEL_OFFSET_Z_MSB_ADDR, etc.).
    // Therefore the last byte must be written whenever the user wants to
    // changes the configuration.

    _write(ACCEL_OFFSET_X_LSB_ADDR, buf[0]);
    _write(ACCEL_OFFSET_X_MSB_ADDR, buf[1]);
    _write(ACCEL_OFFSET_Y_LSB_ADDR, buf[2]);
    _write(ACCEL_OFFSET_Y_MSB_ADDR, buf[3]);
    _write(ACCEL_OFFSET_Z_LSB_ADDR, buf[4]);
    _write(ACCEL_OFFSET_Z_MSB_ADDR, buf[5]);

    _write(MAG_OFFSET_X_LSB_ADDR, buf[6]);
    _write(MAG_OFFSET_X_MSB_ADDR, buf[7]);
    _write(MAG_OFFSET_Y_LSB_ADDR, buf[8]);
    _write(MAG_OFFSET_Y_MSB_ADDR, buf[9]);
    _write(MAG_OFFSET_Z_LSB_ADDR, buf[10]);
    _write(MAG_OFFSET_Z_MSB_ADDR, buf[11]);

    _write(GYRO_OFFSET_X_LSB_ADDR, buf[12]);
    _write(GYRO_OFFSET_X_MSB_ADDR, buf[13]);
    _write(GYRO_OFFSET_Y_LSB_ADDR, buf[14]);
    _write(GYRO_OFFSET_Y_MSB_ADDR, buf[15]);
    _write(GYRO_OFFSET_Z_LSB_ADDR, buf[16]);
    _write(GYRO_OFFSET_Z_MSB_ADDR, buf[17]);

    _write(ACCEL_RADIUS_LSB_ADDR, buf[18]);
    _write(ACCEL_RADIUS_MSB_ADDR, buf[19]);

    _write(MAG_RADIUS_LSB_ADDR, buf[20]);
    _write(MAG_RADIUS_MSB_ADDR, buf[21]);

    mode(lastMode);
}

// read byte from register, return int
uint8_t BNO055_BASE::_read(uint8_t memaddr) {  // memaddr = memory location within the I2C device
    uint8_t buf = 0;
    _readn(&buf, 1, memaddr);
    return buf;
}

// write byte to register
void BNO055_BASE::_write(uint8_t memaddr, uint8_t data) {
    uint8_t buf[2] = {memaddr, data};
    i2c_write_blocking(_i2c, address, buf, 2, false);
}

// read n bytes into buf
bool BNO055_BASE::_readn(uint8_t *buf, int n, uint8_t memaddr) {  // memaddr = memory location within the I2C device
    if (i2c_write_blocking(_i2c, address, &memaddr, 1, true) != 1) {
        return false;
    }
    return i2c_read_blocking(_i2c, address, buf, n, false) == n;
}

int BNO055_BASE::mode(int new_mode) {
    int old_mode = _read(MODE_REGISTER);
    if (new_mode >= 0) {
        _write(MODE_REGISTER, CONFIG_MODE);  // This is empirically necessary if the mode is to be changed
        sleep_ms(20);  // Datasheet table 3.6
        if (new_mode != CONFIG_MODE) {
            _write(MODE_REGISTER, new_mode);
            sleep_ms(10);  // Table 3.6
        }
    }

    _mode = new_mode < 0 ? old_mode : new_mode;
    return old_mode;
}

bool BNO055_BASE::external_crystal() {
    return _read(TRIGGER_REGISTER) & 0x80;
}
