# Downbeat detection

Mixxx knows where the beats of a track are. It does not know where bar one
is. This feature adds the bar phase to the beat grid: which beat of the grid
is beat one, and how many beats a bar has. The analyzer finds the phase, the
waveform shows it, two controls correct it, and the OSC beat message carries
it to the muxic relay.

## What exists

Facts from the tree of 2026-09-21, which is upstream `main` at 89492d5aaa.

### The beat grid of upstream

`src/track/beats.h` holds the model. A `Beats` object has:

- a list of `BeatMarker` values, each one a frame position and a count of the
  beats till the next marker,
- one last marker with a position and a BPM,
- the sample rate and a sub-version string.

A `BeatMarker` marks the border of a tempo section. It carries no bar
information. The comment at `beats.h:62` says that the last marker of a
constant tempo track "is positioned at the first downbeat", but nothing in
the code puts it there, and nothing reads it as a downbeat.

`src/proto/beats.proto` holds the serial format. `BeatGrid` has a BPM and a
first beat. `BeatMap` has a list of beats. Neither one has a bar field.
`Beats::toByteArray()` writes `BeatGrid` for a constant tempo track and
`BeatMap` for a track with tempo markers.

Result: **the beat grid of upstream cannot carry a downbeat today.**

### Upstream pull requests and issues

| Number | State | Author | What it does |
|---|---|---|---|
| 14835 | open | alephlm | Adds `optional int32 downbeats_offset = 3` to the `BeatGrid` message, carries it through `Beats`, adds two controls and an allshader waveform renderer |
| 16491 | open | ronso0 | Takes the `intro_start` cue as the first downbeat. No storage. Draws the bar lines in the allshader beat renderer with a second colour |
| 16978 | open | alephlm | Same drawing as 16491, plus a preference and a track menu item. Adds a `res/schema.xml` revision and a `library` column |
| 16977, 17080 | closed | alephlm | Earlier tries of 16491 |
| 12343 | closed | fwcd | Beat grid editing controls and downbeat lines on the scrolling waveform, on the old `Beats` model |
| 16581 | open | alpham8 | Bar markers, 121 files |
| 16582 | open | alpham8 | Phrase analysis, a data model, a renderer and a detector, 137 files |
| 3036 | open | daschuer | A rhythm analyzer with a tempogram, 37 files, 4216 lines |
| 2877 | open | crisclacerda | A rhythm analyzer, the ancestor of 3036 |
| 15740 | open | n/a | The issue that asks for downbeat estimation |
| 15062, 10164 | open | n/a | Issues that ask for bars and phrases |

Nothing is merged. No upstream release has downbeats. The two current lines
of work are:

- **Storage in the beat grid** (14835). It puts the phase in the protobuf
  blob of `library.beats`. It does not detect anything. The user moves the
  phase by hand with two buttons that wrap at eight beats.
- **No storage** (16491, 16978). The `intro_start` cue is the first downbeat.
  This costs nothing, but it only works when the DJ sets that cue on a bar
  line, and it cannot hold a phase for a track with no intro cue.

None of the pull requests detects the downbeat. PR 3036 detects a time
signature with a tempogram, but it rewrites the whole beat analyzer.

### Forks with a downbeat detector

A search of GitHub forks found no Mixxx fork with a working downbeat
detector. The pull requests above are the whole state of the art in the
Mixxx world.

### The algorithm that the tree already has

`lib/qm-dsp/dsp/tempotracking/DownBeat.cpp` is the bar tracker of the Queen
Mary Vamp plugins. `CMakeLists.txt:4799` compiles it into `mixxx-lib`
already, thus the fork needs no new dependency and no CMake change for the
DSP.

The class follows Davies and Plumbley, "A spectral difference approach to
extracting downbeats in musical audio", EUSIPCO 2006. It:

1. takes the audio at a low rate (the track at 48 kHz decimated by 16, near
   3 kHz),
2. cuts the audio at the known beat positions,
3. takes the magnitude spectrum of each beat segment,
4. measures the Jensen-Shannon divergence between each pair of neighbour
   segments,
5. sums the divergence over each phase candidate, and takes the phase with
   the largest sum.

The method needs the beat positions, thus it runs after the beat analysis.
It does not move a beat. It reports no confidence, and `pushAudioBlock`
needs blocks of one fixed size.

### Machine learning

madmom, BeatNet, beat\_this and all-in-one find downbeats better than a
spectral method, above all outside dance music. Each one needs Python or
ONNX Runtime. The fork runs external programs as jobs already, for the stem
conversion, thus an external "downbeat command" is possible as a second
backend later. This work does not build that path.

## The decision

1. **The phase lives in the beat grid.** The fork adds two optional fields to
   `src/proto/beats.proto` and two members to `Beats`. The field name and the
   field number of the offset are the ones of PR 14835, thus the fork reads
   what that pull request writes, and an upstream merge of 14835 costs
   little. The fork adds the field to `BeatMap` too, because 14835 loses the
   phase on a track with tempo markers.
2. **No fork table.** The phase needs no `muxic_` table, no `res/schema.xml`
   revision and no new column. The blob in `library.beats` grows by a few
   bytes. An older Mixxx skips an unknown protobuf field, thus a downgrade
   loses the phase but reads the grid.
3. **The detector is the qm-dsp bar tracker.** The fork drives
   `DownBeat::findDownBeats` and then scores the phases again from
   `DownBeat::getBeatSD()` to get a confidence. The scoring is the fork's own
   code, thus a test can measure it.
4. **4/4 only.** Beats per bar is 4 for every detected track. The storage
   holds another value, and the controls keep it, but nothing writes another
   value yet.

## The data format

`src/proto/beats.proto`:

```
message BeatGrid {
  optional Bpm bpm = 1;
  optional Beat first_beat = 2;
  optional int32 downbeats_offset = 3;
  optional int32 beats_per_bar = 4;
}

message BeatMap {
  repeated Beat beat = 1;
  optional int32 downbeats_offset = 2;
  optional int32 beats_per_bar = 3;
}
```

`downbeats_offset` is the place of the first downbeat, counted in beats from
the anchor beat of the grid. The anchor beat is the beat at the first marker,
which `Beats::cfirstmarker()` returns. For a track with one tempo the anchor
is the beat at the last marker position. The offset is always in the range 0
to `beats_per_bar - 1`.

The beat with the grid index `i` is a downbeat when
`(i - downbeats_offset) mod beats_per_bar == 0`.

An absent `downbeats_offset` means that the phase is unknown. A track that
nothing analyzed and a track that the detector was not sure about both have
no field. `beats_per_bar` is 4 when the field is absent but the offset is
there.

### What a grid operation does to the phase

| Operation | Effect |
|---|---|
| `tryTranslate` (move the grid by frames) | The phase stays. The beat indices do not change |
| `tryTranslateBeats` (move by whole beats) | The phase stays. The grid slots move with the bars |
| `trySetBpm` | The phase stays. The anchor beat keeps the index 0 |
| `tryScale` (double, halve, and the others) | The offset becomes `round(offset * newBpm / oldBpm) mod beatsPerBar`. A scale that is not a whole number of beats rounds to the nearest beat |

A new beat analysis builds a new grid and drops the phase. The detector then
sets it again.

## The detector

The code is in `src/muxic/downbeats/`.

`DownbeatDetector` takes the audio of the analyzer in the blocks that the
analyzer gives, mixes it to mono, and hands blocks of 1024 frames to the
qm-dsp `DownBeat` class, which decimates them by 16 and buffers them. A ten
minute track costs near 7 MB of memory during the analysis.

When the beat analysis is over, `AnalyzerBeats::storeResults()` calls
`finalize()` with the new beat grid. The detector then:

1. lists the beat positions of the whole track,
2. calls `DownBeat::findDownBeats`, which fills the beat spectral difference
   list,
3. reads that list with `getBeatSD()`,
4. takes the mean difference of each of the four phase candidates, and takes
   the largest one as the phase,
5. counts, for each bar, which of the four transitions had the largest
   difference, and takes the part of the bars that voted for the winner,
6. maps that part to a confidence: `(part - 1/4) / (1 - 1/4)`.

A confidence of 0 means that every phase is equal, which is what a signal
with no bar structure gives. A confidence of 1 means that every bar voted for
the same phase. The detector keeps the phase when the confidence is at least
`kMinConfidence` (0.10) and when the track has at least 16 beats. Below that
it reports nothing, and the grid keeps no phase.

### The limits

- 4/4 only. A track in 3/4, 6/8 or 7/8 gets a wrong phase or no phase.
- One phase for the whole track. A track that changes its bar phase in the
  middle keeps the phase of the majority.
- The method needs a spectral change at the bar line. Electronic dance music
  gives that change. A track with a flat texture, a live recording with a
  free tempo, or an ambient track gives a low confidence.
- The detector needs a beat grid. It runs with the beat analysis. A track
  that Mixxx analyzed before this fork keeps no phase until the user runs
  **Reanalyze** on it.
- Fast analysis reads only the first minute of a track. The phase then comes
  from that minute.

## The controls

`BpmControl` holds them, next to `beats_translate_curpos`.

| Control | What it does |
|---|---|
| `[ChannelN],beats_set_downbeat` | Makes the beat nearest to the play position the first beat of a bar |
| `[ChannelN],beats_downbeat_earlier` | Moves the bar phase one beat earlier |
| `[ChannelN],beats_downbeat_later` | Moves the bar phase one beat later |
| `[ChannelN],beat_in_bar` | Read only. The place of the beat that plays in its bar, 1 to 4. 0 means that the phase is unknown |

The three buttons write the beat grid of the track, thus they need no lock
on the track and they work while the deck plays. A track with a BPM lock or
with no beat grid does not take them.

`beat_in_bar` changes on each beat while the deck plays. It also changes
after a seek, after a loop wrap and after a load. It is 0 while the deck
holds no track, while the track has no grid, and while the grid has no
phase.

The track menu holds **BPM and beatgrid** > **Set downbeat here**, which
does the same as `beats_set_downbeat` on the current play position of the
deck.

No key of the default keyboard mapping is free for these controls. A
controller mapping reaches them by the names above.

## The waveform

The scrolling waveform draws a downbeat line with the beat colour of the
skin at full alpha, and a normal beat line with the same colour at 40 per
cent of that alpha. The bar line is thus stronger, and no skin file changes.
The overview does not draw bar lines.

The fork covers the two renderers that a Mixxx build uses:

- `src/waveform/renderers/allshader/waveformrenderbeat.cpp`, which every
  OpenGL waveform type uses. This is the default.
- `src/waveform/renderers/waveformrenderbeat.cpp`, the QPainter renderer of
  the software waveform types.

## The OSC message

The beat message grows by one argument:

```
/mixxx/ChannelN/beat   h  i  f  i  i
```

| Argument | Type | Meaning |
|---|---|---|
| 1 | h (int64) | The instant of the beat, in nanoseconds on CLOCK_MONOTONIC |
| 2 | i (int32) | The place of the beat in the beat grid of the track |
| 3 | f (float) | The rate that the listener hears, in beats per minute |
| 4 | i (int32) | A counter of the beats that this deck sent |
| 5 | i (int32) | The place of the beat in its bar, 1 to 4. 0 means unknown |

The first four arguments do not change. A reader that takes only four
arguments keeps working.

`beat_in_bar` is also in the default publish list, thus
`/mixxx/ChannelN/beat_in_bar` goes out as a float on each change.

## How the muxic rig uses it

`muxic-sync.mjs` in the muxic repository holds the grid scheduler of
Strudel. Today the owner presses a key to call `muxicSync.downbeat()`, which
tells the scheduler that the current beat is beat one.

With this feature the relay reads argument 5 of the beat message. When it is
1, the beat is a downbeat, and the relay calls the same entry point. The
Strudel bars then follow the bars of the track with no key press. When
argument 5 is 0, the phase is unknown, and the relay keeps the manual
downbeat.

The relay must still watch the deck that the audience hears, because two
decks send two bar phases.

## What is not done

- No bar quantisation. A loop on the bar, a beat jump by bars and a cue
  quantised to the bar are out of scope. The stored phase is what those
  features would need.
- No time signature other than 4/4. Nothing writes `beats_per_bar` with
  another value.
- No machine learning backend. madmom or a similar model as an external
  command is the way to a better result outside dance music.
- No downbeat in the waveform overview and no downbeat in the library.
- No detection for a track that already has a grid. The user must run
  **Reanalyze**.
- The detector is tested on synthetic click tracks only. It is not measured
  against a reference set of real music.
