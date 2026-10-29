/*
******READ BEFORE RUNNING*******
Summary of Code:
This code runs the IMU and GPS code simultaneously and displays their corresponding data.

If Running Code on Raspberry Pi Pico For First Time:
Only one Pi has all of the necessary installed and therefore is fully functional.
Working on the others will not work unless all of the libraries are there. See the
transition document for more information on this.
There is also a specific wiring needed for everything,
this would be in the transition document.

Thing To Know When Modifying Code:
This is a C++ port of the MicroPython main.py. It is built with the Raspberry Pi Pico SDK
(CMakeLists.txt in this folder) instead of being run from Thonny. Building creates main.uf2,
which is flashed by holding BOOTSEL while plugging in the Pico and dragging the file onto it.

Why did we choose MicroPython instead of CircuitPython?:
We needed access to double precision values for the GPS coordiantes contained in this document.
MicroPython and CircuitPython both use floating point values as a default, which causes not enough precision
when using coordinates. This can cause graphs to show a "grid" pattern. To use double precision instead of floating point,
you can modify MicroPython's source files before flashing them to a microcontroller. See Drew Fowler or Aadhavan Srinivasan's
notebooks from Spring 2024 for instructions and more information.
In C++ the coordinates are stored as double, so no custom firmware is needed.

Current Bugs:
Sometimes there are "zero" values read by the GPS sensor (24/10,405 points in most recent test).
Number of IMU update points and IMU refresh rate need to be optimized.
Could also fix FCD screen messages, although these messages are primarily for testing purposes - final deliverable will not have an LCD screen.

Contact Info:
Drew Fowler: fowler52@purdue.edu / anfowler2001@gmail.com
Aadhavan Srinivasan: srini193@purdue.edu
*/

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/uart.h"

#include "bno055.h"  // BNO055 driver (bno055.cpp / bno055_base.cpp)

// INT_MAX in the Python version (renamed because INT_MAX is already a C macro)
constexpr double INT_MAX_COORD = 10000;

// A coordinate as (latitude, longitude) - same as the Python tuples
using Coord = std::array<double, 2>;
using Polygon = std::vector<Coord>;

// Creates coordinate point struct
struct Point {
    double x;
    double y;
    Point(double x, double y) : x(x), y(y) {}
};

const uint LED_PIN = 25;
BNO055 *sensor = nullptr;

// ---------- Small helpers that behave like the Python string functions ----------

// value[start:end] - Python slicing never fails, it just stops at the end of the string
static std::string py_slice(const std::string &value, size_t start, size_t end) {
    if (start >= value.size()) {
        return "";
    }
    return value.substr(start, end - start);
}

// float(value)
static double py_float(const std::string &value) {
    return strtod(value.c_str(), nullptr);
}

// value.strip()
static std::string py_strip(const std::string &value) {
    const char *ws = " \t\r\n";
    size_t first = value.find_first_not_of(ws);
    if (first == std::string::npos) {
        return "";
    }
    size_t last = value.find_last_not_of(ws);
    return value.substr(first, last - first + 1);
}

// value.split(sep)
static std::vector<std::string> py_split(const std::string &value, const std::string &sep) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t pos = value.find(sep, start);
        if (pos == std::string::npos) {
            parts.push_back(value.substr(start));
            return parts;
        }
        parts.push_back(value.substr(start, pos - start));
        start = pos + sep.size();
    }
}

// math.radians(deg)
static double radians(double deg) {
    return deg * M_PI / 180.0;
}

// time.ticks_ms()
static uint32_t ticks_ms() {
    return to_ms_since_boot(get_absolute_time());
}

// Reads one line from the USB serial port (like input()), without the line ending
static std::string read_stdin_line() {
    std::string line;
    while (true) {
        int c = getchar();
        if (c == '\r' || c == '\n') {
            return line;
        }
        line += (char)c;
    }
}

// Set when the computer sends START while we are already running the geofence
static bool restart_requested = false;

// Reads whatever the computer has sent without waiting. Once the geofence is running
// nothing else reads the USB serial port, so without this its buffer fills up and
// computer_end_code.py gets a "Write timeout" the next time it sends START.
static void poll_usb_for_start() {
    static std::string line;
    while (true) {
        int c = getchar_timeout_us(0);
        if (c == PICO_ERROR_TIMEOUT) {
            return;
        }
        if (c == '\r' || c == '\n') {
            if (line.find("START") != std::string::npos) {
                restart_requested = true;
            }
            line.clear();
        } else if (line.size() < 64) {
            line += (char)c;
        }
    }
}

// ---------- GPS UART receive buffer (MicroPython's rxbuf=512) ----------

constexpr int GPS_RXBUF = 512;
static volatile uint8_t gps_rxbuf[GPS_RXBUF];
static volatile int gps_head = 0;
static volatile int gps_tail = 0;

static void gps_uart_irq() {
    while (uart_is_readable(uart0)) {
        uint8_t c = uart_getc(uart0);
        int next = (gps_head + 1) % GPS_RXBUF;
        if (next != gps_tail) {  // Buffer full: the new byte is dropped
            gps_rxbuf[gps_head] = c;
            gps_head = next;
        }
    }
}

static bool gps_any() {
    return gps_head != gps_tail;
}

static uint8_t gps_getc() {
    uint8_t c = gps_rxbuf[gps_tail];
    gps_tail = (gps_tail + 1) % GPS_RXBUF;
    return c;
}

// gps_uart.readline() - reads up to a newline, gives up after 10 ms with no data (timeout=10)
static std::string gps_readline(uart_inst_t *gps_uart) {
    std::string line;
    absolute_time_t deadline = make_timeout_time_ms(10);
    while (true) {
        if (gps_any()) {
            char c = (char)gps_getc();
            line += c;
            if (c == '\n') {
                return line;
            }
            deadline = make_timeout_time_ms(10);
        } else if (time_reached(deadline)) {
            return line;
        }
    }
}

// ---------- Geofence code ----------

double get_latitude(const std::string &value, const std::string &dir) {
    double latDeg = py_float(py_slice(value, 0, 2));
    double latMin = py_float(py_slice(value, 2, 10)) / 60;
    double latitude = (latDeg + latMin);
    if (dir == "S") {
        latitude = -latitude;
    }
    return latitude;
}

// Gets Longitude
double get_longitude(const std::string &value, const std::string &dir) {
    double longDeg = py_float(py_slice(value, 0, 3));
    double longMin = py_float(py_slice(value, 3, 11)) / 60;
    double longitude = (longDeg + longMin);
    if (dir == "W") {
        longitude = -longitude;
    }
    return longitude;
}

// To determine if the coordinate/function q lies on the segment pr
bool onSegment(const Coord &p, const Coord &q, const Coord &r) {

    if ((q[0] <= std::max(p[0], r[0])) &
        (q[0] >= std::min(p[0], r[0])) &
        (q[1] <= std::max(p[1], r[1])) &
        (q[1] >= std::min(p[1], r[1]))) {
        return true;
    }
    return false;
}

// Finding orientation
int orientation(const Coord &p, const Coord &q, const Coord &r) {

    double val = (((q[1] - p[1]) * (r[0] - q[0])) - ((q[0] - p[0]) * (r[1] - q[1])));  // calculating slope

    if (val > 0) {
        return 1;  // positive slope is clockwise orientation
    } else if (val < 0) {
        return 2;  // negative slope is counterclockwise orientation
    } else {
        return 0;  // collinear orientation
    }
}

// Determine if line segment p1q1 and p2q2 intersects
bool doIntersect(const Coord &p1, const Coord &q1, const Coord &p2, const Coord &q2) {

    // looking for orientation
    int o1 = orientation(p1, q1, p2);
    int o2 = orientation(p1, q1, q2);
    int o3 = orientation(p2, q2, p1);
    int o4 = orientation(p2, q2, q1);

    // General Case: If the orientations are different, they intersect
    if ((o1 != o2) && (o3 != o4)) {
        return true;
    }

    // Special Case (collinear): If x and y projection intersects, they intersect
    if ((o1 == 0) && onSegment(p1, p2, q1)) {  // p1, q1 and p2 are collinear and p2 lies on segment p1q1
        return true;
    }

    if ((o2 == 0) && onSegment(p1, q2, q1)) {  // p1, q1 and q2 are collinear and q2 lies on segment p1q1
        return true;
    }

    if ((o3 == 0) && onSegment(p2, p1, q2)) {  // p2, q2 and p1 are collinear and p1 lies on segment p2q2
        return true;
    }

    if ((o4 == 0) && onSegment(p2, q1, q2)) {  // p2, q2 and q1 are collinear and q1 lies on segment p2q2
        return true;
    }

    return false;
}

// Determine if the point p lies within the polygon
bool is_within_polygon(const Polygon &points, const Coord &p) {

    int n = points.size();
    if (n < 3) {  // there must be at least 3 points/vertices in a polygon
        return false;
    }

    Coord extreme = {INT_MAX_COORD, p[1]};  // Create a point for line segment from p to infinite

    int decrease = 0;  // To calculate number of points where y-coordinate of the polygon is equal to y-coordinate of the point
    int count = 0;
    int i = 0;

    while (true) {
        int next = (i + 1) % n;

        if (points[i][1] == p[1]) {
            decrease += 1;
        }

        if (doIntersect(points[i], points[next], p, extreme)) {
            if (orientation(points[i], p, points[next]) == 0) {
                return onSegment(points[i], p, points[next]);
            }

            count += 1;
        }

        i = next;

        if (i == 0) {
            break;
        }

        count -= decrease;
    }

    // Python's % is never negative, so (count % 2 == 1) is matched here
    return ((count % 2) + 2) % 2 == 1;
}

// Turns on the LCD
uart_inst_t *initialize_lcd(uint8_t backlight_red, uint8_t backlight_green, uint8_t backlight_blue) {
    uart_inst_t *lcd_uart = uart1;
    uart_init(lcd_uart, 9600);
    gpio_set_function(4, GPIO_FUNC_UART);  // tx
    gpio_set_function(5, GPIO_FUNC_UART);  // rx
    uart_putc_raw(lcd_uart, '|');
    uart_putc_raw(lcd_uart, 0x18);
    uart_putc_raw(lcd_uart, 0x08);
    uart_putc_raw(lcd_uart, '|');
    uart_putc_raw(lcd_uart, 0x2B);
    uart_putc_raw(lcd_uart, backlight_red);
    uart_putc_raw(lcd_uart, backlight_green);
    uart_putc_raw(lcd_uart, backlight_blue);
    uart_putc_raw(lcd_uart, '|');
    uart_putc_raw(lcd_uart, '-');
    return lcd_uart;
}

// Writes text to the LCD (lcd_uart.write(b"..."))
void lcd_write(uart_inst_t *lcd_uart, const char *text) {
    uart_puts(lcd_uart, text);
}

// Initializes GPS
uart_inst_t *initialize_gps() {
    uart_inst_t *gps_uart = uart0;
    uart_init(gps_uart, 38400);
    gpio_set_function(12, GPIO_FUNC_UART);  // tx
    gpio_set_function(13, GPIO_FUNC_UART);  // rx
    uart_set_format(gps_uart, 8, 1, UART_PARITY_NONE);
    uart_set_hw_flow(gps_uart, false, false);
    uart_set_fifo_enabled(gps_uart, true);

    // Interrupt fills a 512 byte buffer (rxbuf=512 in the Python)
    irq_set_exclusive_handler(UART0_IRQ, gps_uart_irq);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(gps_uart, true, false);
    return gps_uart;
}


// Gets Current Location
std::pair<double, double> get_current_location(uart_inst_t *gps_uart) {

    uint32_t lastDataTime = ticks_ms();

    // Loop continuously until we get a valid satellite fix
    while (true) {
        poll_usb_for_start();
        if (restart_requested) {
            return {0, 0};
        }

        std::string raw = gps_readline(gps_uart);

        // Without this the Pico is silent forever when the GPS is not sending anything
        if (raw.empty()) {
            if (ticks_ms() - lastDataTime > 3000) {
                printf("No data from GPS - check wiring (GPS TX -> GP13) and baud rate (38400)\n");
                lastDataTime = ticks_ms();
            }
        } else {
            lastDataTime = ticks_ms();
            // Decode the line, ignoring garbage bytes from electrical noise
            std::string line;
            for (char c : raw) {
                if ((unsigned char)c < 128) {
                    line += c;
                }
            }
            line = py_strip(line);

            // Look for the GGA sentence ($GNGGA, or $GPGGA on GPS-only modules)
            if (line.rfind("$GNGGA", 0) == 0 || line.rfind("$GPGGA", 0) == 0) {
                std::vector<std::string> parts = py_split(line, ",");

                // Safety check: Ensure the sentence isn't cut off AND has a valid lock (parts[6] != '0')
                if (parts.size() > 6 && parts[6] != "0" && parts[6] != "" && parts[2] != "") {

                    std::string lat = parts[2];
                    std::string lat_dir = parts[3];
                    std::string lon = parts[4];
                    std::string lon_dir = parts[5];

                    // Convert the raw NMEA strings into floating point coordinates
                    double lati = get_latitude(lat, lat_dir);
                    double longi = get_longitude(lon, lon_dir);

                    // Break the loop and return the clean coordinates!
                    return {lati, longi};
                } else {
                    // Print how many satellites it currently sees!
                    std::string sats = parts.size() > 7 ? parts[7] : "0";
                    printf("Waiting for lock... Satellites in view: %s\n", sats.c_str());
                }
            }
        }
    }
}


// Displays IMU sensor information - Used Primarily for Testing Purposes
void imu_stuff() {
    /*
    printf("Temperature: %d degrees C\n", sensor->temperature());
    printf("Accelerometer (m/s^2): ...\n");      // sensor->accel()
    printf("Magnetometer (microteslas): ...\n"); // sensor->mag()
    printf("Gyroscope (rad/sec): ...\n");        // sensor->gyro()
    printf("Euler angle: ...\n");                // sensor->euler()
    printf("Quaternion: ...\n");                 // sensor->quaternion()
    printf("Linear acceleration (m/s^2): ...\n");// sensor->lin_acc()
    printf("Gravity (m/s^2): ...\n");            // sensor->gravity()
    printf("\n");
    sleep_ms(1000);
    */
}

std::pair<Polygon, Polygon> dataReceive() {

    // Flash 3 times fast to signal "I am Alive!"
    for (int _ = 0; _ < 3; _++) {
        gpio_put(LED_PIN, 1);  // On
        sleep_ms(100);
        gpio_put(LED_PIN, 0);  // Off
        sleep_ms(100);
    }

    // Turn LED on solid to show we are entering the listening mode
    gpio_put(LED_PIN, 0);

    printf("listening...\n");

    std::string value;
    while (true) {
        value = py_strip(read_stdin_line());
        // Sometimes Windows sends an extra (or missing) newline - ignore them
        if (value == "") {
            continue;
        }
        printf("Raw Input: %s\n", value.c_str());
        // The Pico doesn't reset when the computer reconnects, so a new START can
        // arrive while we are already listening - say we're ready again and keep waiting
        if (value.find("START") != std::string::npos) {
            printf("listening...\n");
            continue;
        }
        // panic() would stop the Pico reading USB, so ask for the coordinates again instead
        if (value.find("INNER") == std::string::npos) {
            printf("Bad coordinates (INNER is not in list)\n");
            printf("listening...\n");
            continue;
        }
        break;
    }

    // Temporary Geofence For testing purposes - Use computer-end-code.py for real Geofence Values
    // value = "OUTER, 40.39123253146508, -86.82833099365233, 40.365601883498044, -86.97458648681639, 40.42417189314546, -87.00067901611327, 40.468065962237894, -86.85236358642577, 40.428353515662465, -86.79743194580077, INNER";

    std::vector<std::string> coordinateList = py_split(value, ", ");
    int innerBegin = -1;
    for (size_t idx = 0; idx < coordinateList.size(); idx++) {
        if (coordinateList[idx] == "INNER") {
            innerBegin = idx;
            break;
        }
    }
    if (innerBegin < 0) {
        panic("INNER is not in list");
    }

    std::vector<double> outerList;
    for (int idx = 1; idx < innerBegin; idx++) {
        outerList.push_back(py_float(coordinateList[idx]));
    }
    std::vector<double> innerList;
    for (int idx = innerBegin + 1; idx < (int)coordinateList.size(); idx++) {
        innerList.push_back(py_float(coordinateList[idx]));
    }

    // Groups the latitudes and longitudes together
    Polygon outerPolygon;
    for (int i = 0; i < (int)outerList.size() - 1; i += 2) {
        outerPolygon.push_back({outerList[i], outerList[i + 1]});
    }

    // Groups the latitudes and longitudes together
    Polygon innerPolygon;
    for (int i = 0; i < (int)innerList.size() - 1; i += 2) {
        innerPolygon.push_back({innerList[i], innerList[i + 1]});
    }

    return {outerPolygon, innerPolygon};
}


std::tuple<double, double, double, double> imu_update(double latAvg, double longAvg, double time_interval, uint32_t startTime, double velocity_x, double velocity_y) {
    const double earth_radius = 6371000;  // Earth's radius in meters

    double time_elapsed = (int32_t)(ticks_ms() - startTime) / 1000.0;

    auto [imu_acceleration_x, imu_acceleration_y, imu_acceleration_z] = sensor->lin_acc();

    // printf("Accelerations\n");
    printf("Acceleration X: %.10f   Acceleration Y: %.10f\n", imu_acceleration_x, imu_acceleration_y);
    // printf("Sensor Linear Accelearion\n");

    double heading_rad = radians(sensor->euler()[0]);  // yaw angle
    double accel_north = imu_acceleration_x * cos(heading_rad) - imu_acceleration_y * sin(heading_rad);
    double accel_east = imu_acceleration_x * sin(heading_rad) + imu_acceleration_y * cos(heading_rad);

    // Velocity Estimation
    velocity_x += accel_north * time_elapsed;
    velocity_y += accel_east * time_elapsed;

    // printf("VELOCITIES\n");
    // printf("Velocity X: %.10f   Velocity Y: %.10f\n", velocity_x, velocity_y);

    // Position Estimation
    double latitude_change = ((velocity_x * time_elapsed) / earth_radius) * (180 / M_PI);
    double longitude_change = ((velocity_y * time_elapsed) / earth_radius) * (180 / M_PI) / cos(radians(latAvg * (M_PI / 180)));

    // printf("LAT AVG/LONGAVG\n");
    // printf("Latitude: %.10f   Longitude: %.10f\n", latAvg, longAvg);

    // printf("CHANGES\n");
    // printf("Latitude Change: %.10f   Longitude Change: %.10f\n", latitude_change, longitude_change);

    // Update latitude and longitude
    double newlatAvg = latAvg + latitude_change;
    double newlongAvg = longAvg + longitude_change;

    uint32_t endTime = ticks_ms();
    printf("IMU UPDATE\n");
    printf("Latitude: %.10f   Longitude: %.10f\n", newlatAvg, newlongAvg);
    // printf("IMU Refresh Rate: %.1f\n", (double)(endTime - startTime));
    (void)endTime;
    (void)time_interval;

    return {newlatAvg, newlongAvg, velocity_x, velocity_y};
}


int main() {
    stdio_init_all();

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);

    while (true) {
        // Read a full line from the USB serial port
        std::string msg = py_strip(read_stdin_line());

        // Debug: see what we actually got
        // printf("Received: %s\n", msg.c_str());

        if (msg.find("START") != std::string::npos) {
            break;
        }
    }

    Polygon outerPolygon, innerPolygon;
    std::tie(outerPolygon, innerPolygon) = dataReceive();

    // Flash 3 times fast to signal "I am Alive!"
    for (int _ = 0; _ < 3; _++) {
        gpio_put(LED_PIN, 1);  // On
        sleep_ms(100);
        gpio_put(LED_PIN, 0);  // Off
        sleep_ms(100);
    }

    // Turn LED on solid to show we are entering the listening mode
    gpio_put(LED_PIN, 0);

    // I2C Initialization: GP15 (SCL) and GP14 (SDA) use I2C 1
    i2c_init(i2c1, 400000);
    gpio_set_function(15, GPIO_FUNC_I2C);  // SCL
    gpio_set_function(14, GPIO_FUNC_I2C);  // SDA

    // Initialize the IMU using the BNO055 driver in this folder
    printf("Starting IMU...\n");
    static BNO055 imu(i2c1);
    sensor = &imu;
    printf("IMU ready\n");

    int last_val = 0xFFFF;
    (void)last_val;

    uart_inst_t *gps_uart = initialize_gps();  // Initializes GPS
    uart_inst_t *lcd_uart = initialize_lcd(255, 1, 255);
    printf("GPS started, waiting for data...\n");

    // lcd_write(lcd_uart, "Connecting to GPS...            ");  // For 16x2 LCD
    // sleep_ms(1500); - Can add back in to display message for readability on LCD screen. The GPS sensor needs a few seconds to connect usually anyways.

    // Example polygon for testing
    /*
    Polygon outerPolygon = {
        {40.430484, 86.915721},
        {40.430454, 86.915769},
        {40.430806, 86.916144},
        {40.430835, 86.916097}
    };

    Polygon innerPolygon = {
        {40.430484, 86.915721},
        {40.430454, 86.915769},
        {40.430806, 86.916144},
        {40.430835, 86.916097}
    };
    */

    // CHANGE IMU SETTINGS HERE
    int imu_update_points = 10;  // This value can be further optimized. If set to zero, there will be no IMU points (only GPS points).
    double imu_time_interval = 0.1;  // This value can be further optimized. See IMU BNO055 documentation for minimum refresh rate.

    while (true) {
        // The computer was restarted and sent START again - get the new coordinates
        poll_usb_for_start();
        if (restart_requested) {
            restart_requested = false;
            std::tie(outerPolygon, innerPolygon) = dataReceive();
            continue;
        }

        uint32_t startTime = ticks_ms();
        // imu_stuff(); Displays IMU Stuff
        double latitude_avg = 0, longitude_avg = 0;

        std::tie(latitude_avg, longitude_avg) = get_current_location(gps_uart);  // Gets location info
        if (restart_requested) {
            continue;
        }
        uint32_t endTime = ticks_ms();

        double imu_velocity_x = 0;
        double imu_velocity_y = 0;

        printf("GPS POINT BEFORE IMU UPDATES\n");
        printf("Latitude: %.10f   Longitude: %.10f\n", latitude_avg, longitude_avg);  // Prints Lat and Long Info
        printf("GPS Refresh Rate:  %.1f\n", (double)(endTime - startTime));

        if (is_within_polygon(outerPolygon, {latitude_avg, longitude_avg}) == true && is_within_polygon(innerPolygon, {latitude_avg, longitude_avg}) == false) {
            lcd_write(lcd_uart, "IN                              ");  // For 16x2 LCD
            printf("IN\n");
        } else {
            lcd_write(lcd_uart, "OUT                             ");  // For 16x2 LCD
            printf("OUT\n");
        }

        for (int i = 0; i < imu_update_points; i++) {
            poll_usb_for_start();
            if (restart_requested) {
                break;
            }
            startTime = ticks_ms();
            sleep_ms((uint32_t)(imu_time_interval * 1000));
            std::tie(latitude_avg, longitude_avg, imu_velocity_x, imu_velocity_y) = imu_update(latitude_avg, longitude_avg, imu_time_interval, startTime, imu_velocity_x, imu_velocity_y);

            if (is_within_polygon(outerPolygon, {latitude_avg, longitude_avg}) == true && is_within_polygon(innerPolygon, {latitude_avg, longitude_avg}) == false) {
                lcd_write(lcd_uart, "IN                              ");  // For 16x2 LCD
                printf("IN\n");
            } else {
                lcd_write(lcd_uart, "OUT                             ");  // For 16x2 LCD
                printf("OUT\n");
            }

            // printf("TEMP PRINT STATEMENT\n");
            // printf("Latitude: %.10f   Longitude: %.10f\n", latitude_avg, longitude_avg);
        }

        startTime = ticks_ms();

        lcd_write(lcd_uart, "EPICS EVEI                      ");  // For 16x2 LCD

        // FUTURE TEAM COULD FIX THIS
        ////// This would print the information to the LCD - right now this messes up the spacing on the LCD for IN and OUT messages
        // sleep_ms(1500);
        // lcd_write(lcd_uart, "Current Location:               ");  // For 16x2 LCD
        // sleep_ms(1500);
        // lcd_write(lcd_uart, "Lat:  ");  // For 16x2 LCD
        // (write latitude_avg here)
        // lcd_write(lcd_uart, " N   ");
        // sleep_ms(1500);
        // lcd_write(lcd_uart, "Long: ");  // For 16x2 LCD, then longitude_avg and " W   "
        // sleep_ms(1500);

        // lcd_write(lcd_uart, "                ");  // Clear display

        endTime = ticks_ms();
        printf("GPS POINT AFTER IMU UPDATES\n");
        printf("Latitude: %.10f   Longitude: %.10f\n", latitude_avg, longitude_avg);  // Prints Lat and Long Info
        printf("GPS Refresh Rate:  %.1f\n", (double)(endTime - startTime));
    }
}
