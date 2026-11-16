# Geofence

**Website:** https://renbb1016.github.io/Geofence/

Draw the track boundaries on the website, and the computer connected to the
car gets them automatically. No editing files by hand.

## How it works

1. You draw the outer and inner boundaries on the website, save them as a track, and press **Send Track to Car**.
2. The boundaries are stored online in a Supabase database.
3. When you run `computer_end_code.py` on the laptop, it copies them from the database
   into `coordinates.csv`.
4. It then sends `coordinates.csv` to the Pico.

```
Website  ->  Supabase  ->  computer_end_code.py  ->  coordinates.csv  ->  Pico
```

## Using the website

Swipe up (or press the arrow) on the welcome screen, then choose **Create Track** or **Load Track**.

**Create a track**
1. Click **Outer Bounds**, then **+** to start a new bound.
2. Click the map (or press the location button on the map) to drop at least 3 pins, then click **Finish Bound**.
3. Use **<** to go back, and do the same under **Inner Bounds** (optional).
4. Click **Finish Track**, type a name and save. Saving with an existing name updates that track.

**Send it to the car**
1. From the menu, click **Load Track** and pick the track. It previews on the map.
2. Click **Send Track to Car** and confirm. The top right shows what the car is using.

From **Load Track** you can also **Load Into Editor** to change a track, or **Delete Selected Track** (asks first).

Made a mistake? Hover over a pin (or tap it on a phone) and click the red **×** to delete just that pin, drag
the pin to move it, or type new coordinates in the pin list while editing a bound.
**Show Coordinates** lists every point, and **Clear** removes the outer bound, inner bound, or everything.
The 📍 button on the map hides or shows the pins.

Outer bound pins are blue and inner bound pins are orange. Pins are numbered in order.

You can draw several outer and inner bounds, but only the first outer bound and the first inner bound
are saved and sent to the car.

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
