import time
from datetime import date
import serial 
import csv
import json
import math
import os
import tempfile
import urllib.request

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_CSV_PATH = os.path.join(SCRIPT_DIR, "coordinates.csv")
MAX_POINTS = 500
datetime_str = str(date.today()) + '_' + time.strftime("%H-%M-%S", time.localtime())

# def initialize():

#     ser = 0

#     while ser == 0:

#         try:

#             ser = serial.Serial('COM6', 9600, timeout=2) # Times out after 2 seconds of silence

#         except:

#             print("Busy Port: Try closing Raspberry Pi IDE(Thonny) or change \"COM\" parameter")

#     time.sleep(0.6)

#     ser.write(b"START\n")

#     return ser


def initialize():
    ser = None
    while ser is None:
        try:
            ser = serial.Serial('COM4', 115200, timeout=1, write_timeout=5)
        except:
            print("Busy Port: Closing Thonny?")
            time.sleep(1)
            
    # Clear the buffer of old error messages
    ser.reset_input_buffer()
    
    # Send the START command with a carriage return
    print("Sending START signal...")
    try:
        ser.write(b"START\r\n")
    except serial.SerialTimeoutException:
        # The Pico is connected but not reading what we send (usually old firmware still
        # running the geofence from the last run). Restarting it puts it back to waiting for START.
        print("The Pico is not reading data. Unplug it and plug it back in (or press reset), then run this again.")
        ser.close()
        raise SystemExit(1)

    return ser

def serialWrite(x, ser):
    print("Waiting for Pico to wake up...")

    # STEP 1: Wait for Pico's "listening..." prompt
    while True:
        # readline() reads a full line at a time, making it much cleaner!
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        
        if line:
            print(line)
            if "listening..." in line:
                break # The Pico is ready! Break out of this waiting loop.

    # STEP 2: Send the coordinates
    cmd = str(x) + '\r\n' 
    print(f"\n--- Pico is ready! Sending Coordinates ---")

    # Send the coordinates only once, in small chunks. The Pico only reads one line,
    # so sending them twice fills its USB buffer and write() blocks forever.
    for i in range(0, len(cmd), 64):
        ser.write(cmd[i:i+64].encode('utf-8'))
        time.sleep(0.05)
    ser.flush()

    print("--- Coordinates Sent! Waiting for sensor stream ---\n")

    # STEP 3: Listen forever for incoming GPS/IMU data
    while True:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            
            # If we received actual text (not just a 2-second timeout blank)
            if line: 
                print(line)
                with open(f'datalog{datetime_str}.txt', 'a') as f:
                    f.write(f'{line}\n')
                    
        except serial.SerialException:
            print("\nError: The Pico disconnected unexpectedly.")
            break

        
# ----------------------------------------------------------------------
# Getting the boundaries from the website (through Supabase)
#
# The website's "Set Final Bounds For Car" button stores the boundaries in
# Supabase. Each time this program starts, it copies them into coordinates.csv:
#
#     OUTER
#     lat,lon
#     ...
#     INNER
#     lat,lon
#     ...
#
# The CSV is only replaced with a complete, validated file. If Supabase or the
# internet is unavailable, or the data is invalid, the existing CSV is kept.
#
# Settings are read from a .env file next to this script (see .env.example).
# ----------------------------------------------------------------------

def load_env(path):
    """Read KEY=VALUE lines from a .env file into os.environ (if not already set)."""
    if not os.path.exists(path):
        return
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, value = line.split("=", 1)
            os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


def load_config():
    load_env(os.path.join(SCRIPT_DIR, ".env"))

    url = os.environ.get("SUPABASE_URL", "").rstrip("/")
    key = os.environ.get("SUPABASE_ANON_KEY", "")
    if not url or not key:
        raise ValueError("SUPABASE_URL and SUPABASE_ANON_KEY must be set in .env")

    csv_path = os.environ.get("CSV_PATH", "coordinates.csv")
    if not os.path.isabs(csv_path):
        csv_path = os.path.join(SCRIPT_DIR, csv_path)

    return {"url": url, "key": key, "csv_path": csv_path}


def fetch_active_track(config):
    """Return the boundaries sent to the car, or None if nothing has been sent."""
    endpoint = (
        config["url"]
        + "/rest/v1/active_track"
        + "?id=eq.1&select=name,outer_bound,inner_bound,updated_at"
    )
    request = urllib.request.Request(endpoint, headers={
        "apikey": config["key"],
        "Authorization": "Bearer " + config["key"],
        "Accept": "application/json",
    })
    with urllib.request.urlopen(request, timeout=15) as response:
        rows = json.loads(response.read().decode("utf-8"))

    if not isinstance(rows, list) or len(rows) != 1:
        raise ValueError(f"Unexpected response from active_track: {rows!r}")

    track = rows[0]

    # Nothing has been sent from the website yet
    if track.get("outer_bound") in (None, []):
        return None

    return track


def is_number(value):
    return (isinstance(value, (int, float))
            and not isinstance(value, bool)
            and math.isfinite(value))


def validate_bound(bound, min_points, label):
    """Return a list of (lat, lon) floats, or raise ValueError."""
    if not isinstance(bound, list):
        raise ValueError(f"{label} bound is not a list")

    if len(bound) == 0 and min_points == 0:
        return []

    if len(bound) < max(min_points, 3) or len(bound) > MAX_POINTS:
        raise ValueError(f"{label} bound has {len(bound)} points")

    points = []
    for point in bound:
        if (not isinstance(point, list) or len(point) != 2
                or not is_number(point[0]) or not is_number(point[1])):
            raise ValueError(f"{label} bound has a bad point: {point!r}")

        lat, lon = float(point[0]), float(point[1])
        if not (-90 <= lat <= 90) or not (-180 <= lon <= 180):
            raise ValueError(f"{label} bound point out of range: {point!r}")

        points.append((lat, lon))

    return points


def build_csv(track):
    outer = validate_bound(track.get("outer_bound"), 3, "Outer")
    inner = validate_bound(track.get("inner_bound", []), 0, "Inner")

    lines = ["OUTER"]
    lines += [f"{lat!r},{lon!r}" for lat, lon in outer]
    lines.append("INNER")
    lines += [f"{lat!r},{lon!r}" for lat, lon in inner]

    return "\n".join(lines) + "\n"


def read_current(path):
    try:
        with open(path, "r", encoding="utf-8") as f:
            return f.read()
    except FileNotFoundError:
        return None


def write_atomic(path, text):
    """Write to a temp file in the same folder, then swap it in."""
    folder = os.path.dirname(path) or "."
    fd, temp_path = tempfile.mkstemp(prefix=".coordinates_", suffix=".tmp", dir=folder)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
            f.flush()
            os.fsync(f.fileno())

        # On Windows the replace fails if another program has the file open
        # at that moment, so retry for a short while.
        for attempt in range(10):
            try:
                os.replace(temp_path, path)
                return
            except PermissionError:
                if attempt == 9:
                    raise
                time.sleep(0.3)
    finally:
        if os.path.exists(temp_path):
            os.remove(temp_path)


def sync_once(config):
    """Copy the boundaries from Supabase into the CSV if they have changed."""
    track = fetch_active_track(config)
    if track is None:
        print("Nothing sent to the car from the website yet; CSV left unchanged.")
        return

    new_text = build_csv(track)

    if read_current(config["csv_path"]) == new_text:
        print(f"Boundaries already up to date (track '{track.get('name')}').")
        return

    write_atomic(config["csv_path"], new_text)
    print(f"Updated {config['csv_path']} from track '{track.get('name')}' "
          f"(saved {track.get('updated_at')}).")


def sync_before_run():
    """Update the CSV from Supabase if possible. Returns the CSV path to send."""
    csv_path = DEFAULT_CSV_PATH
    try:
        config = load_config()
        csv_path = config["csv_path"]
        sync_once(config)
    except Exception as error:
        print(f"WARNING: Could not update the boundaries ({error}).")
        print("The car will use the last saved boundaries.")

    return csv_path


def get_coordinates(csv_path):
    try:
        filename = open(csv_path, 'r')
        data = list(csv.reader(filename, delimiter = ","))
        filename.close()
    except FileNotFoundError:
        print(f"Could not find {csv_path}! Check your file path.")
        return ""

    new_data = []
    for row in range(len(data)):
        for col in range(len(data[row])):
            if (data[row][col] == "OUTER" or data[row][col] == "INNER"):
                new_data.append(data[row][col])
            else:
                new_data.append(str(abs(float(data[row][col]))))

    return ', '.join(new_data) 

if __name__ == '__main__':
    coordinate_data = get_coordinates(sync_before_run())
    if coordinate_data:
        serialWrite(coordinate_data, initialize())      