# Geofence

EPICS EVEI geofence: draw a track's outer and inner boundaries on the website,
and the computer connected to the Pico picks them up automatically.

```
index.html (website)  ->  Supabase  ->  sync_boundaries.py  ->  coordinates.csv  ->  computer_end_code.py  ->  Pico
```

| File | Purpose |
|------|---------|
| `index.html` | Map for drawing bounds, saving named tracks and choosing the track the car uses |
| `supabase/schema.sql` | Database tables, validation and access rules (run once) |
| `sync_boundaries.py` | Runs in the background and keeps `coordinates.csv` up to date |
| `computer_end_code.py` | Reads `coordinates.csv` and sends it to the Pico over serial (unchanged) |

## 1. Supabase setup (once)

1. Create a project at https://supabase.com.
2. Open **SQL Editor -> New query**, paste the contents of `supabase/schema.sql` and press **Run**.
3. Open **Project Settings -> API** and copy the **Project URL** and the **anon public** key.
   Do not use the `service_role` key anywhere; nothing in this project needs it.

## 2. Website

Put the Project URL and anon key into the `SUPABASE_URL` / `SUPABASE_ANON_KEY`
constants near the top of the script in `index.html`. The anon key is meant to
be public; what it can do is limited by the rules in `schema.sql` (it can add
and edit tracks but cannot delete them, and invalid coordinates are rejected).

To host it, open the repo on GitHub: **Settings -> Pages -> Deploy from a branch -> main / (root)**.
You can also just open `index.html` in a browser.

Using it:
1. Draw an outer bound (and optionally an inner bound) and press **Finish Current Bound**.
2. Type a track name and press **Save Track**.
3. Press **Use for Car** to make it the active track.

Only the first finished outer bound and the first finished inner bound are saved,
because `coordinates.csv` holds one of each.

## 3. Computer running the geofence program

1. Copy `.env.example` to `.env` and fill in the URL and anon key.
   Set `CSV_PATH` to the `coordinates.csv` that `computer_end_code.py` reads
   (by default, the one in this folder). `.env` is git-ignored; never commit it.
2. Test once:
   ```
   python sync_boundaries.py --once
   ```
3. Keep it running in the background by starting it automatically at log-on
   (Windows Task Scheduler):
   - **Create Task...** -> name it `Geofence sync`
   - **Triggers:** At log on
   - **Actions:** Start a program
     - Program: full path to `pythonw.exe` (run `where pythonw` to find it)
     - Arguments: `sync_boundaries.py`
     - Start in: this folder
   - **Settings:** check "If the task fails, restart every 1 minute"

The script checks Supabase every `POLL_SECONDS` (10 s by default) and logs to
`sync_boundaries.log`. If the internet or Supabase is down, or the data is
invalid, it keeps the existing CSV and retries with increasing delays (up to
5 minutes). The CSV is replaced in one step, so `computer_end_code.py` never
reads a half-written file.

New boundaries are sent to the Pico the next time `computer_end_code.py` is started.
