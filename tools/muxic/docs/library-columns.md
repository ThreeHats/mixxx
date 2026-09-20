# More library columns

The library track table gets four columns: energy, danceability, tags and
loudness. The fork also takes the saved column layout of upstream pull
request 14479.

The muxic hub owns the data. It computes energy, danceability and tags in
Python (`hub/muxichub/quality.py`) and writes them into a fork table. Mixxx
reads the table, shows the values, sorts and searches by them, and can edit
energy and tags.

## The columns

| Column | Header | Type | Source |
|---|---|---|---|
| `muxic_energy` | Energy | integer 1 to 10 | the table below |
| `muxic_danceability` | Danceability | real 0 to 1 | the table below |
| `muxic_tags` | Tags | text | the table below |
| `muxic_lufs` | Loudness (LUFS) | real | computed from `library.replaygain` |

All four columns are hidden by default. Right-click the table header to show
one. All four sort. Energy and tags are editable in the table and in the track
properties dialog. Danceability and loudness are read only.

These views show the columns: Tracks, Hidden Tracks, Missing Tracks, a crate, a
playlist, Auto DJ and History. They all read one cache, `library_cache_view`,
which the fork extends with a LEFT JOIN.

The Computer view (a file browser) and the external library views (iTunes,
Rekordbox, Traktor, Rhythmbox, Serato) do not show the columns. They have
tables of their own and no row in `library`.

## The table

The data access object makes the table when the database opens. There is no
new revision in `res/schema.xml` and no new column in `library`.

```sql
CREATE TABLE IF NOT EXISTS muxic_track_meta (
    track_id INTEGER PRIMARY KEY REFERENCES library(id),
    muxic_energy INTEGER,
    muxic_danceability REAL,
    muxic_tags TEXT,
    updated_at INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS muxic_track_meta_updated_at
    ON muxic_track_meta (updated_at);
```

- `track_id` is `library.id`, not `track_locations.id`. A move of the file
  keeps the row.
- A missing row is the same as a row of NULL values. The table shows an empty
  cell.
- `updated_at` is the time of the last write, in milliseconds since the epoch
  (UTC). Mixxx polls this column, thus every writer must set it.
- Mixxx deletes the row when the track leaves the library.

## The tag format

`muxic_tags` holds the tags between commas, with a comma before the first tag
and after the last one:

```
,bass,two words,vocal,
```

A tag is lower case and trimmed. An inner run of spaces becomes one space. An
empty tag is dropped, and a repeat is dropped. An empty list is NULL, not an
empty string.

The commas at both ends make an exact search cheap: `muxic_tags LIKE '%,vocal,%'`
matches the tag `vocal` and not the tag `vocalist`. The `%` and `_` of a tag
get a backslash, and the query says `ESCAPE '\'`.

## The loudness column

Mixxx already measures EBU R128 loudness. `AnalyzerEbur128` subtracts the
measured loudness from the ReplayGain 2.0 reference of -18 LUFS and stores the
result as a gain ratio in `library.replaygain`. The loudness is thus a pure
function of a value that the library holds, and it needs no data of its own:

```
lufs = -18 - 20 * log10(replaygain_ratio)
```

The view selects `library.replaygain AS muxic_lufs`, and the cache converts the
ratio to LUFS. `MuxicLufsTest.MatchesTheReplayGainReference` proves the math
against `db2ratio`.

Two limits:

- A ratio of 0 means "not analyzed". The cell stays empty.
- A gain that comes from a file tag, not from the Mixxx analyzer, can use
  another reference. The loudness is then as good as that tag.

The sort of the column inverts the ratio (`ORDER BY -muxic_lufs`), because a
high ratio means a quiet track. Ascending order puts the quiet tracks first.

## Search

| Query | Effect |
|---|---|
| `energy:7` | energy of exactly 7 |
| `energy:>=7` | energy of 7 or more |
| `energy:5-8` | energy from 5 to 8 |
| `energy:""` | no energy value |
| `danceability:>0.7` | danceability above 0.7 |
| `tag:vocal` | the tag `vocal`, whole and exact |
| `tag:"two words"` | a tag with a space |
| `-tag:vocal` | no tag `vocal`, and tracks with no tag at all |
| `tag:""` | no tag at all |

`en:` is short for `energy:`. `dance:` is short for `danceability:`. `tags:` is
the same as `tag:`. The search field tooltip lists the keywords.

A tag argument takes the same normalization as a stored tag, thus `tag:Vocal`
finds `vocal`.

## How the hub writes the table

The hub writes `mixxxdb.sqlite` directly. One row per track:

```sql
INSERT INTO muxic_track_meta (track_id, muxic_energy, muxic_danceability,
                              muxic_tags, updated_at)
VALUES (?, ?, ?, ?, ?)
ON CONFLICT(track_id) DO UPDATE SET
    muxic_energy = excluded.muxic_energy,
    muxic_danceability = excluded.muxic_danceability,
    muxic_tags = excluded.muxic_tags,
    updated_at = excluded.updated_at;
```

Set `updated_at` to `int(time.time() * 1000)`. Write the tags in the stored
form, with the two outer commas.

If Mixxx is closed, it reads the new values at the next start. If Mixxx runs,
it finds them in at most five seconds: `TrackMetaDao` polls
`WHERE updated_at > <last seen>` every 5000 ms, and it tells the library cache
to read the rows again. The table then repaints. This is the only path that
brings an outside change into a running Mixxx, thus a writer that does not set
`updated_at` stays invisible until the next start.

An edit inside Mixxx takes the same path: the data access object writes the
row, then reports the track id at once.

## The saved column layout

The header context menu gets three entries, from pull request 14479 by ronso0:

- **Save columns layout** stores the current header state as the common one.
- **Load saved columns layout** applies the common state to this view.
- **Sync with saved columns layout** keeps this view on the common state. The
  view then also saves into the common state.

The common state lives in the `settings` table under
`common_header_state_pb`. A per-view state stays in
`<namespace>.header_state_pb`, as before. A load of the common state does not
force a new column to be visible and does not change the sort column, because
the views do not have the same columns. The Computer view cannot take the
common state.

## What is not done

- Energy shows as a number. There is no bar and no color delegate.
- The multi-track properties dialog (`DlgTrackInfoMulti`) has no muxic group.
  Edit energy and tags in the table, or one track at a time.
- There is no search on the loudness column.
- The muxic search keywords work in the library views only. In an external
  library view (iTunes, Rekordbox, Traktor, Rhythmbox, Serato) the table has no
  muxic column, thus the query fails and the view stays empty. Upstream has the
  same limit with other keywords.
- Mixxx writes energy and tags only. Danceability comes from the hub.
