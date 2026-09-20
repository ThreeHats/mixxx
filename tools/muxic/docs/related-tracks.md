# Related tracks

A relation joins two library tracks that mix well. Mixxx keeps the
relations in its own table, shows them in a sidebar node, and writes a new
relation from the track menu or from a control.

## The table

The DAO makes the table when the database opens. There is no new revision
in `res/schema.xml`.

```sql
CREATE TABLE IF NOT EXISTS muxic_track_relations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    source_track_id INTEGER NOT NULL REFERENCES library(id),
    target_track_id INTEGER NOT NULL REFERENCES library(id),
    bidirectional INTEGER NOT NULL DEFAULT 0,
    relation_type TEXT NOT NULL DEFAULT '',
    rating INTEGER NOT NULL DEFAULT 0,
    notes TEXT NOT NULL DEFAULT '',
    datetime_added TEXT DEFAULT CURRENT_TIMESTAMP,
    UNIQUE (source_track_id, target_track_id)
);
CREATE INDEX IF NOT EXISTS muxic_track_relations_target
    ON muxic_track_relations (target_track_id);
```

| Column | Content |
|---|---|
| `id` | The row key |
| `source_track_id` | `library.id` of the track the relation starts at |
| `target_track_id` | `library.id` of the track the relation leads to |
| `bidirectional` | 0 = one way, 1 = both ways |
| `relation_type` | Free text, for example `mix`, `harmonic_blend`, `mashup` |
| `rating` | 0 to 5. 0 means no rating |
| `notes` | Free text |
| `datetime_added` | UTC, in the SQLite `CURRENT_TIMESTAMP` format |

Both ends are `library.id`, not `track_locations.id`. The dedupe tool of the
muxic hub moves a library row to a new location, and the relation stays.

### Rules

- A pair of tracks has at most one row. A write of the reverse pair updates
  the row that exists and sets `bidirectional` to 1.
- A relation of a track with itself is refused.
- A purge of a track deletes each relation that has this track at an end.
- The repair step deletes a relation that points at a track which is not in
  `library` any more, and a relation of a track with itself.

## How the hub reads and writes the table

The hub (`hub/muxichub/mixxxdb.py`) can read and write this table while
Mixxx is closed. Mixxx reads the table again at the next start, thus a hub
write needs no reload. To write from Python:

```python
cur.execute(
    "INSERT OR IGNORE INTO muxic_track_relations "
    "(source_track_id, target_track_id, bidirectional, relation_type, rating, notes) "
    "VALUES (?, ?, ?, ?, ?, ?)",
    (source_id, target_id, 1, "harmonic_blend", 4, "long blend"),
)
```

To keep one row per pair, the hub must look for the reverse pair first:

```python
row = cur.execute(
    "SELECT id FROM muxic_track_relations "
    "WHERE source_track_id=? AND target_track_id=?",
    (target_id, source_id),
).fetchone()
if row:
    cur.execute(
        "UPDATE muxic_track_relations SET bidirectional=1 WHERE id=?", (row[0],))
```

To read the tracks that follow a track:

```sql
SELECT target_track_id FROM muxic_track_relations WHERE source_track_id = :id
UNION
SELECT source_track_id FROM muxic_track_relations
    WHERE target_track_id = :id AND bidirectional <> 0;
```

The table has no trigger and no foreign-key action. The hub must delete the
relations of a track that it removes from `library`.

## The sidebar node

The node "Related Tracks" is next to Crates. It has two groups:

```
Related Tracks
    Related
        Selected track
        Deck 1 ... Deck N
    Suggestions
        Selected track
        Deck 1 ... Deck N
```

- The root node and both group nodes show every track that has a relation.
- A node under "Related" shows the tracks that a relation leads to from its
  reference track: the track chosen in the library, or the track on that
  deck.
- A node under "Suggestions" shows the tracks that fit the tempo and the
  key of its reference track. See Suggestions below.
- The number of deck nodes follows `[App],num_decks`.

Every node of this feature has the normal library columns and five more:
"Relation" (the type), "Relation Rating", "Relation Note", "Direction" and
"Relations" (the count of the relations of that track). The direction is an
arrow: → for one way, ↔ for both ways. The first four columns are empty in
a node that shows no single relation per row. The column set is the same in
each node, thus the table keeps the column layout when the node changes.

A one-way relation appears under the track at its start only. A both-ways
relation appears under both tracks.

To remove a relation, select its row in a "Related" node and push Remove
(Ctrl+Del), or use "Remove" in the track menu. Under a reference track this
removes the relation to that track. Under the root node it removes each
relation of the selected tracks.

## Suggestions

The Suggestions view is one SQL query. There is no background job and
nothing is written.

- Tempo: within 3 % of the tempo of the reference track, of its half tempo,
  or of its double tempo.
- Key: the same key, the relative major or minor, or one step in each
  direction on the Camelot wheel. This is the set of
  `KeyUtils::getCompatibleKeys()`, which walks the Circle of Fifths.
  Upstream pull request 16554 by FaithfulSparrow uses the same set to tint
  the library rows by harmonic fit.
- If the reference track has a tempo and a key, both filters apply. If it
  has only one of them, only that filter applies. If it has neither, the
  view is empty.

## The track menu

The track menu gets three items:

- **Relate to** > **Deck N: Artist - Title**. This writes a one-way
  relation from each selected track to the track on that deck. The type is
  `mix` and the rating is 0. To make the relation go both ways, use the
  entry a second time from the other track.
- **Relate to** > **Remove Relations**. This deletes every relation of the
  selected tracks. The item is off when no selected track has a relation.
- **Show Related Tracks**. This opens the node "Related > Selected track"
  with the clicked track as the reference. The item needs one selected
  track.

The deck entries are read when the submenu opens.

## The control

| Control | Type |
|---|---|
| `[Library],relate_active_decks` | Push button |

The control writes a both-ways relation of the tracks on two decks, with
the type `mix`:

1. If exactly two decks play and hold a track, the control relates those
   two decks.
2. If not, and deck 1 and deck 2 both hold a track, the control relates
   deck 1 and deck 2.
3. If not, the control does nothing.

A controller script sends it with
`engine.setValue("[Library]", "relate_active_decks", 1)`. A keyboard
mapping binds it with a line `relate_active_decks <key>` under `[Library]`
in the `.kbd.cfg` file.

## Code

| Path | Content |
|---|---|
| `src/muxic/relatedtracks/trackrelation.h` | The relation value type |
| `src/muxic/relatedtracks/trackrelationschema.h` | The table and column names |
| `src/muxic/relatedtracks/trackrelationstorage.{h,cpp}` | The DAO |
| `src/muxic/relatedtracks/relationsuggester.{h,cpp}` | The tempo and key rules |
| `src/muxic/relatedtracks/relatedtrackstablemodel.{h,cpp}` | The table model |
| `src/muxic/relatedtracks/relatedtracksfeature.{h,cpp}` | The sidebar node and the control |
| `src/test/trackrelationstorage_test.cpp` | The DAO tests |
| `src/test/relationsuggester_test.cpp` | The tempo and key tests |

The fork code is in the namespace `muxic`.

The upstream files that change are `src/library/trackcollection.{h,cpp}`
(the DAO joins the database and the purge), `src/library/library.{h,cpp}`
(the node and the signal `showRelatedTracks`), `src/widget/wtrackmenu.{h,cpp}`
(the menu items) and `CMakeLists.txt`.

## What is not done

- The menu cannot set the type, the rating or the note of a relation. Only
  the hub can write them. A dialog is the next step.
- The relation columns are not sorted by a `SortColumnId`, thus a click on
  one of their headers sorts by the raw column value.
- The node uses the icon of Crates. It has no icon of its own.
- The deck nodes carry a plain label. They do not show the title of the
  track on the deck.
- The Suggestions view does not read the tags or the genre.
