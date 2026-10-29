// bno055_base.h Minimal driver for Bosch BNO055 nine degree of
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

#pragma once

#include <array>
#include <cstdint>
#include "hardware/i2c.h"

constexpr uint8_t ACCEL_OFFSET_X_LSB_ADDR = 0x55;
constexpr uint8_t ACCEL_OFFSET_X_MSB_ADDR = 0x56;
constexpr uint8_t ACCEL_OFFSET_Y_LSB_ADDR = 0x57;
constexpr uint8_t ACCEL_OFFSET_Y_MSB_ADDR = 0x58;
constexpr uint8_t ACCEL_OFFSET_Z_LSB_ADDR = 0x59;
constexpr uint8_t ACCEL_OFFSET_Z_MSB_ADDR = 0x5A;

constexpr uint8_t MAG_OFFSET_X_LSB_ADDR = 0x5B;
constexpr uint8_t MAG_OFFSET_X_MSB_ADDR = 0x5C;
constexpr uint8_t MAG_OFFSET_Y_LSB_ADDR = 0x5D;
constexpr uint8_t MAG_OFFSET_Y_MSB_ADDR = 0x5E;
constexpr uint8_t MAG_OFFSET_Z_LSB_ADDR = 0x5F;
constexpr uint8_t MAG_OFFSET_Z_MSB_ADDR = 0x60;

constexpr uint8_t GYRO_OFFSET_X_LSB_ADDR = 0x61;
constexpr uint8_t GYRO_OFFSET_X_MSB_ADDR = 0x62;
constexpr uint8_t GYRO_OFFSET_Y_LSB_ADDR = 0x63;
constexpr uint8_t GYRO_OFFSET_Y_MSB_ADDR = 0x64;
constexpr uint8_t GYRO_OFFSET_Z_LSB_ADDR = 0x65;
constexpr uint8_t GYRO_OFFSET_Z_MSB_ADDR = 0x66;

constexpr uint8_t ACCEL_RADIUS_LSB_ADDR = 0x67;
constexpr uint8_t ACCEL_RADIUS_MSB_ADDR = 0x68;
constexpr uint8_t MAG_RADIUS_LSB_ADDR = 0x69;
constexpr uint8_t MAG_RADIUS_MSB_ADDR = 0x6A;

using Tuple3 = std::array<double, 3>;
using Tuple4 = std::array<double, 4>;

class BNO055_BASE {
public:
    BNO055_BASE(i2c_inst_t *i2c, uint8_t address = 0x28, bool crystal = true);
    virtual ~BNO055_BASE() {}

    void reset();

    Tuple3 mag();          // microteslas (x, y, z)
    Tuple3 accel();        // m.s^-2
    Tuple3 lin_acc();      // m.s^-2
    Tuple3 gravity();      // m.s^-2
    Tuple3 gyro();         // deg.s^-1
    Tuple3 euler();        // degrees (heading, roll, pitch)
    Tuple4 quaternion();   // (w, x, y, z)

    int temperature();

    // Return [sys, gyro, accel, mag] calibration data.
    std::array<uint8_t, 4> cal_status();
    bool calibrated();

    std::array<uint8_t, 22> sensor_offsets();
    void set_offsets(const uint8_t *buf);

    int mode(int new_mode = -1);
    bool external_crystal();

    uint8_t address;
    bool crystal;

protected:
    // Used by the subclass so it can set itself up before reset() runs
    // (the Python subclass sets transpose/sign before calling super().__init__).
    struct DeferInit {};
    BNO055_BASE(i2c_inst_t *i2c, uint8_t address, bool crystal, DeferInit);
    void init();

    // Python checks hasattr(self, "orient"); the base class has nothing to do.
    virtual void orient() {}

    void scaled_tuple(uint8_t addr, double scale, double *out, int count);

    // read byte from register, return int
    uint8_t _read(uint8_t memaddr);
    // write byte to register
    void _write(uint8_t memaddr, uint8_t data);
    // read n bytes into buf, returns false on I2C error
    bool _readn(uint8_t *buf, int n, uint8_t memaddr);

    i2c_inst_t *_i2c;
    int _mode;
};
