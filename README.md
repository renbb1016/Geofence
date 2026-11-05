# Geofence

**Website:** https://renbb1016.github.io/Geofence/

Draw the track boundaries on the website, and the computer connected to the
car gets them automatically. No editing files by hand.

## How it works

1. You draw the outer and inner boundaries on the website and press **Set Final Bounds For Car**.
2. The boundaries are stored online in a Supabase database.
3. When you run `computer_end_code.py` on the laptop, it copies them from the database
   into `coordinates.csv`.
4. It then sends `coordinates.csv` to the Pico.

```
Website  ->  Supabase  ->  computer_end_code.py  ->  coordinates.csv  ->  Pico
```

## Using the website

1. Click **+ New Outer Bound**, then click the map (or press **My Location**) to drop at least 3 pins.
2. Click **Finish Current Bound**.
3. Do the same with **+ New Inner Bound** (optional).
4. Click the green **Set Final Bounds For Car** button at the top. The top right shows what the car is using.

Made a mistake? Hover over a pin (or tap it on a phone) and click the red **×** to delete just that pin, or drag
the pin to move it.

Saving is optional, for tracks you want to reuse:
- **Save Track**: type a name and press Enter. Saving with an existing name updates that track.
- **Load Track**: pick a track and press Enter. Then press **Set Final Bounds For Car** to send it.
- **Delete a track**: in **Load Track**, pick it and click the 🗑 trash can, then **Yes, delete** (or **No** to go back).

Outer bound pins are blue and inner bound pins are orange.

Only the first outer bound and the first inner bound are used.

## Running it on the laptop

First time only:
- Copy `.env.example` to `.env` and fill in the Supabase URL and anon key.

Every time:
```
python computer_end_code.py
```

The boundaries are only fetched when the program starts, so restart it after
setting new ones on the website.

If the internet is down, the program prints a warning and sends the last saved
`coordinates.csv`, so check for that warning if the car should be using new
boundaries. It also refuses to save bad or incomplete boundaries.

## Files

| File | What it does |
|------|--------------|
| `index.html` | The website |
| `computer_end_code.py` | Copies the selected track from Supabase into `coordinates.csv`, sends it to the Pico and logs GPS data |
| `supabase/schema.sql` | Sets up the database (paste into Supabase SQL Editor and run once) |
| `.env.example` | Template for the laptop's settings |

Never put the Supabase `service_role` / secret key in any of these files.
