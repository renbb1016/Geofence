-- Geofence boundary storage.
-- Run once in the Supabase dashboard: SQL Editor -> New query -> paste -> Run.
--
-- Safe to run again after changes; it only adds what is missing.
--
-- A "bound" is a JSON array of [lat, lng] pairs, e.g. [[40.431, -86.915], ...]
-- The website writes these; sync_boundaries.py reads the boundaries in
-- active_track and writes them to coordinates.csv in the OUTER / INNER format.

-- ------------------------------------------------------------------
-- Validation (same rules as the website and sync_boundaries.py)
-- ------------------------------------------------------------------
create or replace function public.is_valid_bound(b jsonb, min_pts int)
returns boolean
language plpgsql
immutable
as $$
declare
    p jsonb;
    n int;
begin
    if b is null or jsonb_typeof(b) <> 'array' then
        return false;
    end if;

    n := jsonb_array_length(b);

    -- Empty is allowed only when min_pts = 0 (inner bound is optional)
    if n = 0 then
        return min_pts = 0;
    end if;

    if n < greatest(min_pts, 3) or n > 500 then
        return false;
    end if;

    for p in select * from jsonb_array_elements(b) loop
        if jsonb_typeof(p) <> 'array'
           or jsonb_array_length(p) <> 2
           or jsonb_typeof(p->0) <> 'number'
           or jsonb_typeof(p->1) <> 'number'
           or (p->>0)::double precision not between -90 and 90
           or (p->>1)::double precision not between -180 and 180 then
            return false;
        end if;
    end loop;

    return true;
end;
$$;

-- ------------------------------------------------------------------
-- Tables
-- ------------------------------------------------------------------
create table if not exists public.tracks (
    id          uuid primary key default gen_random_uuid(),
    name        text not null unique
                check (char_length(btrim(name)) between 1 and 80),
    outer_bound jsonb not null
                check (public.is_valid_bound(outer_bound, 3)),
    inner_bound jsonb not null default '[]'::jsonb
                check (public.is_valid_bound(inner_bound, 0)),
    updated_at  timestamptz not null default now()
);

-- Single row (id = 1) saying which track the car should use
create table if not exists public.active_track (
    id         int primary key default 1 check (id = 1),
    track_id   uuid references public.tracks(id),
    updated_at timestamptz not null default now()
);

insert into public.active_track (id, track_id)
values (1, null)
on conflict (id) do nothing;

-- The boundaries the car should use, copied here by "Use for Car" so a
-- drawing can be sent to the car without saving it as a track.
-- An empty outer_bound means nothing has been sent yet.
-- (track_id above is no longer used.)
alter table public.active_track
    add column if not exists name        text,
    add column if not exists outer_bound jsonb not null default '[]'::jsonb,
    add column if not exists inner_bound jsonb not null default '[]'::jsonb;

alter table public.active_track
    drop constraint if exists active_track_outer_valid,
    drop constraint if exists active_track_inner_valid,
    drop constraint if exists active_track_name_length;

alter table public.active_track
    add constraint active_track_outer_valid
        check (outer_bound = '[]'::jsonb or public.is_valid_bound(outer_bound, 3)),
    add constraint active_track_inner_valid
        check (public.is_valid_bound(inner_bound, 0)),
    add constraint active_track_name_length
        check (name is null or char_length(name) <= 80);

-- Keep updated_at current
create or replace function public.touch_updated_at()
returns trigger
language plpgsql
as $$
begin
    new.updated_at := now();
    return new;
end;
$$;

drop trigger if exists tracks_touch on public.tracks;
create trigger tracks_touch
    before update on public.tracks
    for each row execute function public.touch_updated_at();

drop trigger if exists active_track_touch on public.active_track;
create trigger active_track_touch
    before update on public.active_track
    for each row execute function public.touch_updated_at();

-- ------------------------------------------------------------------
-- Row Level Security
-- Anyone with the site (anon key) can read, add and edit tracks and pick
-- the active track. No DELETE policies exist, so rows cannot be deleted
-- from the website. The service-role key is not needed by any component.
-- ------------------------------------------------------------------
alter table public.tracks       enable row level security;
alter table public.active_track enable row level security;

drop policy if exists "tracks read"   on public.tracks;
drop policy if exists "tracks insert" on public.tracks;
drop policy if exists "tracks update" on public.tracks;

create policy "tracks read"   on public.tracks
    for select to anon, authenticated using (true);
create policy "tracks insert" on public.tracks
    for insert to anon, authenticated with check (true);
create policy "tracks update" on public.tracks
    for update to anon, authenticated using (true) with check (true);

drop policy if exists "active read"   on public.active_track;
drop policy if exists "active update" on public.active_track;

create policy "active read"   on public.active_track
    for select to anon, authenticated using (true);
create policy "active update" on public.active_track
    for update to anon, authenticated using (id = 1) with check (id = 1);

-- Belt and braces: remove delete/truncate rights from public roles
revoke delete, truncate on public.tracks       from anon, authenticated;
revoke delete, truncate on public.active_track from anon, authenticated;
