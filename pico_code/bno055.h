// bno055.h Driver for Bosch nine degree of freedom inertial
// measurement unit module with sensor fusion.

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
// Ported from MicroPython (bno055.py) to C++ for the Raspberry Pi Pico SDK.

#pragma once

#include <vector>
#include "bno055_base.h"

constexpr int CONFIG_MODE = 0x00;
constexpr int ACCONLY_MODE = 0x01;
constexpr int MAGONLY_MODE = 0x02;
constexpr int GYRONLY_MODE = 0x03;
constexpr int ACCMAG_MODE = 0x04;
constexpr int ACCGYRO_MODE = 0x05;
constexpr int MAGGYRO_MODE = 0x06;
constexpr int AMG_MODE = 0x07;
constexpr int IMUPLUS_MODE = 0x08;
constexpr int COMPASS_MODE = 0x09;
constexpr int M4G_MODE = 0x0a;
constexpr int NDOF_FMC_OFF_MODE = 0x0b;
constexpr int NDOF_MODE = 0x0c;

constexpr int ACC = 0x08;  // Registers for configuration (page 1)
constexpr int MAG = 0x09;
constexpr int GYRO = 0x0a;

constexpr int ACC_DATA = 0x08;  // Data regsiters (page 0)
constexpr int MAG_DATA = 0x0e;
constexpr int GYRO_DATA = 0x14;
constexpr int GRAV_DATA = 0x2e;
constexpr int LIN_ACC_DATA = 0x28;
constexpr int EULER_DATA = 0x1a;
constexpr int QUAT_DATA = 0x20;

class BNO055 : public BNO055_BASE {
public:
    static constexpr int acc_range[] = {2, 4, 8, 16};  // G
    static constexpr int acc_bw[] = {8, 16, 31, 62, 125, 250, 500, 1000};
    static constexpr int gyro_range[] = {2000, 1000, 500, 250, 125};  // dps
    static constexpr int gyro_bw[] = {523, 230, 116, 47, 23, 12, 64, 32};  // bandwidth (Hz)
    static constexpr int mag_rate[] = {2, 6, 8, 10, 15, 20, 25, 30};  // rate (Hz)

    // Transposition (x, y, z) 0 == x 1 == y 2 == z hence (0, 1, 2) is no change
    // Scaling (x, y, z) 0 == normal 1 == invert
    BNO055(i2c_inst_t *i2c, uint8_t address = 0x28, bool crystal = true,
           std::array<int, 3> transpose = {0, 1, 2}, std::array<int, 3> sign = {0, 0, 0});

    // Configuration: pass {range, bw} (or {rate} for MAG) to change it, or {} to only read it.
    // Returns the old config in human readable form, or {} if it could not be decoded.
    std::vector<int> config(int dev, std::vector<int> value = {});

    // For use in ISR
    void iget(uint8_t reg);

    std::array<int, 3> sign;
    std::array<int, 3> transpose;
    int w;
    int x;
    int y;
    int z;

protected:
    void orient() override;

private:
    static int _tuple_to_int(int dev, const std::vector<int> &v);  // Convert (range, bw) to register value
    static std::vector<int> _int_to_tuple(int dev, int v);  // Return the current config in human readable form
    static int _bytes_toint(int lsb, int msb);  // Convert two bytes to signed integer (little endian)

    uint8_t buf6[6];
    uint8_t buf8[8];
};
