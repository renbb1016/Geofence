"""
Keeps coordinates.csv in sync with the active track stored in Supabase.

The website's "Use for Car" button stores the boundaries in Supabase. This
script polls Supabase, and whenever those boundaries change it rewrites
coordinates.csv in the same format computer_end_code.py already reads:

    OUTER
    lat,lon
    ...
    INNER
    lat,lon
    ...

The CSV is only replaced with a complete, validated file. If Supabase or the
internet is unavailable, or the data is invalid, the existing CSV is kept.

Usage:
    python sync_boundaries.py          # run forever (poll every POLL_SECONDS)
    python sync_boundaries.py --once   # sync one time and exit (0 = ok, 1 = error)

Settings are read from a .env file next to this script (see .env.example).
"""

import json
import logging
import math
import os
import sys
import tempfile
import time
import urllib.error
import urllib.request
from logging.handlers import RotatingFileHandler

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
MAX_POINTS = 500
MAX_BACKOFF_SECONDS = 300

log = logging.getLogger("sync_boundaries")


def setup_logging():
    log.setLevel(logging.INFO)
    fmt = logging.Formatter("%(asctime)s %(levelname)s %(message)s")

    file_handler = RotatingFileHandler(
        os.path.join(SCRIPT_DIR, "sync_boundaries.log"),
        maxBytes=500_000, backupCount=2
    )
    file_handler.setFormatter(fmt)
    log.addHandler(file_handler)

    # pythonw has no console, so only log to the console when there is one
    if sys.stderr is not None:
        console = logging.StreamHandler()
        console.setFormatter(fmt)
        log.addHandler(console)


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
        raise SystemExit("SUPABASE_URL and SUPABASE_ANON_KEY must be set in .env")

    csv_path = os.environ.get("CSV_PATH", "coordinates.csv")
    if not os.path.isabs(csv_path):
        csv_path = os.path.join(SCRIPT_DIR, csv_path)

    poll = float(os.environ.get("POLL_SECONDS", "10"))

    return {"url": url, "key": key, "csv_path": csv_path, "poll": max(poll, 2)}


# ----------------------------------------------------------------------
# Supabase
# ----------------------------------------------------------------------

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


# ----------------------------------------------------------------------
# Validation and CSV
# ----------------------------------------------------------------------

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


# ----------------------------------------------------------------------
# Main loop
# ----------------------------------------------------------------------

def sync_once(config):
    """Sync one time. Returns True if the CSV was changed."""
    track = fetch_active_track(config)
    if track is None:
        log.info("Nothing sent to the car from the website yet; CSV left unchanged")
        return False

    new_text = build_csv(track)

    if read_current(config["csv_path"]) == new_text:
        return False

    write_atomic(config["csv_path"], new_text)
    log.info("Updated %s from track '%s' (saved %s)",
             config["csv_path"], track.get("name"), track.get("updated_at"))
    return True


def main():
    setup_logging()
    config = load_config()
    once = "--once" in sys.argv

    log.info("Syncing active track to %s", config["csv_path"])

    delay = config["poll"]
    while True:
        try:
            sync_once(config)
            delay = config["poll"]
            if once:
                return 0

        except (urllib.error.URLError, TimeoutError, ConnectionError) as error:
            log.warning("Cannot reach Supabase (%s); keeping current CSV", error)
            if once:
                return 1
            delay = min(delay * 2, MAX_BACKOFF_SECONDS)

        except ValueError as error:
            log.error("Invalid boundary data (%s); keeping current CSV", error)
            if once:
                return 1
            delay = min(delay * 2, MAX_BACKOFF_SECONDS)

        except Exception:
            log.exception("Unexpected error; keeping current CSV")
            if once:
                return 1
            delay = min(delay * 2, MAX_BACKOFF_SECONDS)

        time.sleep(delay)


if __name__ == "__main__":
    sys.exit(main())
