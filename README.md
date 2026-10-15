# Geofence

**Website:** https://renbb1016.github.io/Geofence/

Draw the track boundaries on the website, and the computer connected to the
car gets them automatically. No editing files by hand.

## How it works

1. You draw the outer and inner boundaries on the website and press **Set Final Bounds For Car**.
2. The boundaries are stored online in a Supabase database.
3. A small program on the laptop (`sync_boundaries.py`) checks the database every
   10 seconds and copies them into `coordinates.csv`.
4. When you run `computer_end_code.py`, it sends `coordinates.csv` to the Pico like always.

```
Website  ->  Supabase  ->  sync_boundaries.py  ->  coordinates.csv  ->  computer_end_code.py  ->  Pico
```

## Using the website

1. Click **+ New Outer Bound**, then click the map (or press **My Location**) to drop at least 3 pins.
2. Click **Finish Current Bound**.
3. Do the same with **+ New Inner Bound** (optional).
4. Click the green **Set Final Bounds For Car** button at the top. The top right shows what the car is using.

Made a mistake? Click the red **×** next to a pin to delete just that pin, or drag
the pin to move it.

Saving is optional, for tracks you want to reuse:
- **Save Track**: type a name and press Enter. Saving with an existing name updates that track.
- **Load Track**: pick a track and press Enter. Then press **Set Final Bounds For Car** to send it.

Only the first outer bound and the first inner bound are used.

## Running it on the laptop

First time only:
- Copy `.env.example` to `.env` and fill in the Supabase URL and anon key.

Every time:
1. Start the sync program and leave it open:
   ```
   python sync_boundaries.py
   ```
2. Run the car program like normal (from this folder):
   ```
   python computer_end_code.py
   ```

New boundaries go to the Pico the next time you start `computer_end_code.py`.

If the internet goes down, nothing breaks. The sync program keeps the last good
`coordinates.csv` and tries again later. It also refuses to write bad or
incomplete boundaries.

Optional: to have the sync program start by itself when you log in, use Windows
Task Scheduler to run `pythonw sync_boundaries.py` from this folder at log on.

## Files

| File | What it does |
|------|--------------|
| `index.html` | The website |
| `sync_boundaries.py` | Copies the selected track from Supabase into `coordinates.csv` |
| `computer_end_code.py` | Sends `coordinates.csv` to the Pico and logs GPS data |
| `supabase/schema.sql` | Sets up the database (paste into Supabase SQL Editor and run once) |
| `.env.example` | Template for the laptop's settings |

Never put the Supabase `service_role` / secret key in any of these files.
