// bno055.cpp Driver for Bosch nine degree of freedom inertial
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

#include "bno055.h"

#include "pico/stdlib.h"

namespace {

constexpr uint8_t PAGE_REGISTER = 0x07;
constexpr uint8_t AXIS_MAP_SIGN = 0x42;
constexpr uint8_t AXIS_MAP_CONFIG = 0x41;

// Same as Python's tuple.index(): position of value, or -1 if it is not there
template <size_t N>
int index_of(const int (&table)[N], int value) {
    for (size_t i = 0; i < N; i++) {
        if (table[i] == value) {
            return (int)i;
        }
    }
    return -1;
}

}  // namespace

// Convert (range, bw) to register value
int BNO055::_tuple_to_int(int dev, const std::vector<int> &v) {
    if (dev == ACC) {
        int r = v.size() > 0 ? index_of(acc_range, v[0]) : -1;
        int b = v.size() > 1 ? index_of(acc_bw, v[1]) : -1;
        if (r < 0 || b < 0) {
            panic("Illegal accel range %d or bandwidth %d", v.size() > 0 ? v[0] : 0, v.size() > 1 ? v[1] : 0);
        }
        return r | (b << 2);
    } else if (dev == GYRO) {
        int r = v.size() > 0 ? index_of(gyro_range, v[0]) : -1;
        int b = v.size() > 1 ? index_of(gyro_bw, v[1]) : -1;
        if (r < 0 || b < 0) {
            panic("Illegal gyro range %d or bandwidth %d", v.size() > 0 ? v[0] : 0, v.size() > 1 ? v[1] : 0);
        }
        return r | (b << 3);
    } else if (dev == MAG) {
        int r = v.size() > 0 ? index_of(mag_rate, v[0]) : -1;
        if (r < 0) {
            panic("Illegal magnetometer rate %d", v.size() > 0 ? v[0] : 0);
        }
        return r;
    }
    return 0;
}

// Return the current config in human readable form
std::vector<int> BNO055::_int_to_tuple(int dev, int v) {
    if (dev == ACC) {
        if ((v >> 2) >= 8) {
            return {};  // Can occur e.g. initial config of magnetometer
        }
        return {acc_range[v & 3], acc_bw[v >> 2]};
    } else if (dev == GYRO) {
        if ((v & 7) >= 5 || (v >> 3) >= 8) {
            return {};
        }
        return {gyro_range[v & 7], gyro_bw[v >> 3]};
    } else if (dev == MAG) {
        if (v >= 8) {
            return {};
        }
        return {mag_rate[v]};
    }
    panic("Unknown device. %d", dev);
}

// Convert two bytes to signed integer (little endian) Can be used in an interrupt handler
int BNO055::_bytes_toint(int lsb, int msb) {
    if (!(msb & 0x80)) {
        return msb << 8 | lsb;  // +ve
    }
    // Same operator order as the Python: the + 1 happens before the |
    return -(((msb ^ 255) << 8) | ((lsb ^ 255) + 1));
}

// Transposition (x, y, z) 0 == x 1 == y 2 == z hence (0, 1, 2) is no change
// Scaling (x, y, z) 0 == normal 1 == invert
BNO055::BNO055(i2c_inst_t *i2c, uint8_t address, bool crystal,
               std::array<int, 3> transpose, std::array<int, 3> sign)
    : BNO055_BASE(i2c, address, crystal, DeferInit{}) {
    // std::array is always 3 elements, so only the values need checking
    for (int s : sign) {
        if (s != 0 && s != 1) {
            panic("Sign values must be 0 or 1");
        }
    }
    this->sign = sign;
    bool seen[3] = {false, false, false};
    for (int t : transpose) {
        if (t < 0 || t > 2 || seen[t]) {
            panic("Transpose indices must be unique and in range 0-2");
        }
        seen[t] = true;
    }
    this->transpose = transpose;
    init();  // super().__init__(i2c, address, crystal)
    w = 0;
    x = 0;
    y = 0;
    z = 0;
}

void BNO055::orient() {
    if (transpose != std::array<int, 3>{0, 1, 2}) {
        const std::array<int, 3> &a = transpose;
        _write(AXIS_MAP_CONFIG, (a[2] << 4) + (a[1] << 2) + a[0]);
    }
    if (sign != std::array<int, 3>{0, 0, 0}) {
        const std::array<int, 3> &a = sign;
        _write(AXIS_MAP_SIGN, a[2] + (a[1] << 1) + (a[0] << 2));
    }
}

// Configuration: if a tuple is passed, convert to int using function from bno055_help.py
std::vector<int> BNO055::config(int dev, std::vector<int> value) {
    if (dev != ACC && dev != MAG && dev != GYRO) {
        panic("Unknown device: %d", dev);
    }
    bool has_value = !value.empty();
    int reg_value = has_value ? _tuple_to_int(dev, value) : 0;  // Convert tuple to register value
    int last_mode = mode(CONFIG_MODE);
    _write(PAGE_REGISTER, 1);
    int old_val = _read(dev);
    if (has_value) {
        _write(dev, reg_value);
    }
    _write(PAGE_REGISTER, 0);
    mode(last_mode);
    return _int_to_tuple(dev, old_val);
}

// For use in ISR
void BNO055::iget(uint8_t reg) {
    int n;
    uint8_t *buf;
    int len;
    if (reg == 0x20) {
        n = 4;
        buf = buf8;
        len = sizeof(buf8);
    } else {
        n = 3;
        buf = buf6;
        len = sizeof(buf6);
    }
    _readn(buf, len, reg);
    int i;
    if (n == 4) {
        w = _bytes_toint(buf[0], buf[1]);
        i = 2;
    } else {
        w = 0;
        i = 0;
    }
    x = _bytes_toint(buf[i], buf[i + 1]);
    y = _bytes_toint(buf[i + 2], buf[i + 3]);
    z = _bytes_toint(buf[i + 4], buf[i + 5]);
}
