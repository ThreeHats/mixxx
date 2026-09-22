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
| 16491 | open | ronso0 | Takes the `intro_start` cue as the first downbeat. No storage. Draws the bar lines in the allshader beat renderer with a second color |
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
ONNX Runtime. The fork runs external programs from a command template
already, for the stem conversion. The second detector of this feature takes
the same way. See **The external detector**.

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
   `DownBeat::getBeatSD()`. The scoring is the fork's own code, thus a test
   can measure it, and it takes a phase only when the vote passes a
   significance test.
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
  optional int32 beats_per_bar = 100;
}

message BeatMap {
  repeated Beat beat = 1;
  optional int32 downbeats_offset = 100;
  optional int32 beats_per_bar = 101;
}
```

`BeatGrid.downbeats_offset` takes the number 3 of upstream pull request
14835, thus the fork reads what that work writes. The other three fields are
of this fork alone. They start at 100, where a later upstream field cannot
meet them and read a fork value as its own.

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
minute track at 44.1 kHz holds 8.4 MB of decimated audio, and the buffer
doubles when it is full, thus the peak is 12.6 MB. A twenty minute track
costs twice that. The memory goes back when the analysis of the track ends.

When the beat analysis is over, `AnalyzerBeats::storeResults()` calls
`finalize()` with the beat position list of the beat plugin, which is the
same list that builds the grid. The detector then:

1. cuts the list where the buffered audio ends,
2. calls `DownBeat::findDownBeats`, which fills the beat spectral difference
   list,
3. reads that list with `getBeatSD()`,
4. takes the mean difference of each of the four phase candidates, and takes
   the largest one as the phase,
5. counts, for each bar, which of the four transitions had the largest
   difference, and counts the bars that voted for the winner.

### Why a vote share is not enough

A share of the votes says nothing by itself. With no bar structure each bar
votes for the winner with the chance 1/4, thus a track of 16 bars reaches a
large share often. A measurement on flat input showed that a rule of "a
share over 0.10 above chance" took a phase 91 percent of the time at 16
beats and 26 percent of the time at 256 beats. A wrong phase is worse than
none, because the relay keeps the manual downbeat only while the bar is
unknown.

The votes follow a binomial law with the chance 1/4 under no bar structure.
The detector takes the phase only when

```
votes >= bars / 4 + 3 * sqrt(bars * 3 / 16)
```

which is three standard deviations over the mean of that law. On flat input
this takes a phase about once in a hundred tracks, at every track length. A
track needs at least 64 beats, which are 16 bars; a shorter track gets no
phase at all.

The detector also reports a confidence, which is the part of the bars that
voted for the winner after the quarter that chance gives:
`(part - 1/4) / (1 - 1/4)`. It goes to the log and to the tests. It is a
report, not the rule.

### The limits

- 4/4 only. A track in 3/4, 6/8 or 7/8 gets a wrong phase or no phase.
- One phase for the whole track. A track that changes its bar phase in the
  middle keeps the phase of the majority.
- The method needs a spectral change at the bar line. Electronic dance music
  gives that change. A track with a flat texture, a live recording with a
  free tempo, or an ambient track gets no phase.
- A track under 64 beats gets no phase. At 128 beats per minute that is a
  track under 30 seconds.
- The detector needs the beats of the analysis. It runs with the beat
  analysis. A track that Mixxx analyzed before this fork keeps no phase until
  the user runs **Reanalyze** on it.
- A beat plugin that reports no beat positions, such as SoundTouch, gets no
  downbeat step at all.
- Fast analysis reads only the first minute of a track. The phase then comes
  from that minute.

### Measured on real music

Two electronic dance tracks of the rig, at 128 and at 134 BPM with a
constant tempo, got no phase from the detector. The probe test
`src/test/downbeatprobe_test.cpp` (it runs only with `MUXIC_DOWNBEAT_PROBE`
set to a directory of audio files) shows why:

| Track | Beats | Mean change into the beat, phase 0 to 3 | Winner of each quarter |
|---|---|---|---|
| A, 128 BPM | 451 | 0.355, 0.285, 0.358, 0.309 | 2, 0, 3, 0 |
| B, 134 BPM | 665 | 0.335, 0.336, 0.341, 0.316 | 1, 2, 0, 2 |

The qm-dsp `DownBeat` class takes the phase with the largest mean spectral
change into the beat, and it puts a downbeat on every fourth beat from
there. Its output is one phase by construction, thus its histogram says
nothing about confidence. On these tracks the four means lie within 3
percent of each other, and the winner changes from one quarter of the track
to the next. The spectral difference between beats is not a downbeat clue on
dense dance music, where each beat carries a kick and the bass runs through
the bar. The vote then stays at chance, and the detector reports no phase.
That is the correct answer of this detector, not a fault of the threshold. A
lower threshold marks wrong bars, which is worse than no bar.

A second feature was measured on the same two tracks: the low band energy
after each beat by phase, and the step of the energy from one beat to the
next by phase. Both put phase 0 first on both tracks, but two tracks prove
nothing, and no listener confirmed the phase.

**The external detector finds a phase on this music.** See **The numbers on
real music** below.

The controls below work regardless. Set the downbeat by hand at the first
bar, and the grid, the waveform and the OSC message carry it.

## The external detector

A machine learning beat tracker finds the bar on this music, where the
spectral method does not. The fork runs such a tracker as an external
program, in the way the stem conversion runs its separator: a command
template with placeholders, no shell, and the program found on the path.

`src/muxic/downbeats/externaldownbeatdetector.*` holds the code.

### What it does

1. It puts the track file in the `$INPUT` placeholder of the template and a
   path in a work directory in `$OUTPUT`, and starts the program.
2. It reads the output file. When that file is absent or empty, it reads
   the standard output of the program.
3. It reads each line as a time in seconds and a place of the beat in its
   bar. A line with the place 1 is a downbeat. A line that holds no number
   is skipped. Tab, space, comma and semicolon all separate the two fields.
4. It maps each downbeat time to the nearest beat of the grid that Mixxx
   computed. A downbeat before the first beat or after the last one drops
   out.
5. It counts, for each place of the bar, the downbeats that fall on it, and
   hands the histogram to `DownbeatDetector::scoreVotes`, which is the same
   significance test that the built in detector uses.

The phase comes from the vote, not from the labels of the program. A
program that finds the bars of a track but a tempo that the Mixxx grid does
not share gives a scattered histogram, and the test then gives no phase.

### The order of the two detectors

| Setting | What runs |
|---|---|
| Downbeat detector: built in | The qm-dsp detector alone |
| Downbeat detector: external command | The command first. The qm-dsp detector runs when the command fails, finds no downbeat, or gives a histogram that the test refuses |

The log carries one line for each track:

```
muxic downbeat: <file> source "external" accepted true phase 3 confidence 0.739
```

### The settings

The BPM page of the preferences holds three fields under the downbeat
checkbox: the detector, the command and the timeout. They go to `mixxx.cfg`
in the group `[BeatDetection]`:

```
[BeatDetection]
DownbeatDetector external
DownbeatCommand beat_this --gpu 0 -o "$OUTPUT" "$INPUT"
DownbeatTimeoutSeconds 120
```

| Item | Default | Meaning |
|---|---|---|
| `DownbeatDetector` | `builtin` | `builtin` or `external` |
| `DownbeatCommand` | `beat_this --gpu 0 -o "$OUTPUT" "$INPUT"` | The program and its arguments |
| `DownbeatTimeoutSeconds` | 120 | Mixxx kills the program after this time |

The template holds no shell. A quote groups a word, `$INPUT` and `$OUTPUT`
take their values, and any other `$NAME` is an error. Write the full path of
the program when the desktop session of the user does not carry its
directory on `PATH`.

The command runs on the analyzer thread, which is neither the GUI thread nor
the engine thread, thus the wait there costs nothing to the sound. The
detector reads the stop flag of that thread five times a second: a cancelled
analysis kills the program.

### Install beat\_this

[beat\_this](https://github.com/CPJKU/beat_this) of CPJKU is the tracker
that this work measured. It goes into a `uv` tool environment, as demucs
did:

```
uv tool install --python 3.10 \
    --with "torch==2.5.1" --with "torchaudio==2.5.1" \
    "https://github.com/CPJKU/beat_this/archive/main.zip"
```

The program then sits in `~/.local/bin/beat_this`. The first run downloads
the model `final0.ckpt` to `~/.cache/torch/hub/checkpoints`, which takes
about 6 seconds. `--gpu 0` picks the first NVIDIA card, and `--gpu -1` runs
on the processor.

beat\_this writes one line for each beat: the time in seconds, a tab, and
the place of the beat in its bar, with 1 for the first beat of a bar. That
is the format that the parser reads.

### The numbers on real music

Four tracks of the library of the rig, at 44.1 kHz. The command was
beat\_this on a GeForce RTX 2060. **A** is one of the two tracks that got no
phase from the built in detector.

| Track | Beats | Built in | External | Votes of the external run, phase 0 to 3 |
|---|---|---|---|---|
| A, Bad Girl, 128 BPM | 451 | no phase (0.060) | **phase 0**, confidence 0.376 | 58, 50, 0, 1 |
| B, Do It To It, 125 BPM | 262 | phase 3 (0.303) | **phase 3**, confidence 0.508 | 24, 0, 0, 41 |
| C, Gem World, 150 BPM | 567 | phase 3 (0.300) | **phase 3**, confidence 0.739 | 12, 15, 0, 111 |
| D, levitation, 136 BPM | 370 | no phase (0.116) | **phase 2**, confidence 0.436 | 10, 2, 45, 21 |

The external detector found a phase on all four tracks. The built in
detector found one on two of them, and it agreed with the external detector
on both.

No listener marked the bars of these tracks, thus the check is the two
features that the probe prints: the low band energy after each beat by
phase, and the step of the energy from one beat to the next by phase.

| Track | Largest low band energy | Largest energy step | External phase |
|---|---|---|---|
| A | phase 0 (tie with 1) | phase 0 | 0 |
| B | phase 3 | phase 3 | 3 |
| C | phase 3 | phase 3 | 3 |
| D | phase 2 | phase 2 | 2 |

The external phase is the phase that both features put first, on all four
tracks. The earlier measurement put phase 0 first on track A by the same
features, and the machine learning tracker says phase 0 as well.

Track A is the weak one: 58 votes for phase 0 against 50 for phase 1. The
beat grid of Mixxx and the beats of the tracker walk one beat apart over the
track, thus the second half of the track votes for the neighbour phase. The
significance test still takes phase 0, and phase 0 holds for a little over
half of the track.

Run times on the GeForce RTX 2060, with the model in the cache:

| Track | Length | beat\_this on the card | beat\_this on the processor |
|---|---|---|---|
| A | 3:31 | 3.4 s | 6.5 s |
| B | 2:05 | 3.1 s | |
| C | 3:47 | 3.4 s | |
| D | 2:41 | 3.2 s | |

A run costs about 3 seconds for each track, thus a library of 5000 tracks
costs about 4 hours of card time. The analysis of the track itself, which
Mixxx does in the same job, costs more.

### How to measure again

The probe test runs the command on real files:

```
MUXIC_DOWNBEAT_PROBE=<directory of audio files> \
MUXIC_DOWNBEAT_COMMAND='beat_this --gpu 0 -o "$OUTPUT" "$INPUT"' \
    build/mixxx-test --gtest_filter='DownbeatProbeTest.*'
```

It prints, for each file, the beats, the numbers of the built in detector,
the two energy features, and the phase, the confidence, the vote histogram
and the run time of the command.

## Detect downbeats on a track that has a grid

The library of the rig is analyzed already, and a new analysis would build a
new beat grid. The track menu holds **Adjust BPM** > **Detect Downbeats**
for that case. It puts the tracks in the analysis queue with the option
`downbeatOnly`, and `AnalyzerBeats` then:

- keeps the beat grid of the track and reads the beats from it,
- runs the detectors in the order above,
- writes the grid again only when a detector found a phase.

The item works in the library and on a deck. A track with no beat grid and a
track with a BPM lock get no downbeat step. The item runs even while the
downbeat checkbox of the preferences is off, because the user asked for it. The analysis still reads the
whole file, because the built in detector needs the audio; a run with the
external command alone would not, but the queue decodes the file in any
case.

## The controls

`BpmControl` holds them, next to `beats_translate_curpos`.

| Control | What it does |
|---|---|
| `[ChannelN],beats_set_downbeat` | Makes the beat nearest to the play position the first beat of a bar |
| `[ChannelN],beats_downbeat_earlier` | Moves the bar phase one beat earlier |
| `[ChannelN],beats_downbeat_later` | Moves the bar phase one beat later |
| `[ChannelN],beat_in_bar` | Read only. The place of the beat that plays in its bar, 1 to 4. 0 means that the phase is unknown |

The three buttons write the beat grid of the track, thus they take the lock
of the track. They run on the thread of the GUI or of the controller, where
that lock is safe, and they work while the deck plays. A track with a BPM
lock or with no beat grid does not take them.

`beat_in_bar` changes on each beat while the deck plays. It also changes
after a seek, after a loop wrap and after a load. It is 0 while the deck
holds no track, while the track has no grid, and while the grid has no
phase.

The track menu of a deck holds **Adjust BPM** > **Set Downbeat Here**, which
writes `beats_set_downbeat` of that deck. The item is grey in the library,
because only a deck has a play position.

The default keyboard mapping maps no beat grid control at all, not even
`beats_translate_curpos`, thus these three controls get no key either. A
controller mapping or a keyboard file of the user reaches them by the names
above.

### The button in LateNight

The beat grid button row of each deck in LateNight has a tall button with
one long bar and three short bars, between the beat shift buttons and the
Undo and Lock column. A left click sets the downbeat to the beat closest to
the play position. A right click moves the downbeat one beat later, thus four
right clicks go once around the bar. The files: `res/skins/LateNight/waveform.xml`,
the two style sheets, `btn__downbeat.svg` in each scheme, and the tooltip
`beats_set_downbeat` in `src/skin/legacy/tooltips.cpp`. Deere and the other
skins have no button. Their mappings reach the controls by name.

## The waveform

The scrolling waveform makes the bar line stronger than the beat line, with
the beat color of the skin. No skin file changes.

- `allshader/waveformrenderbeat.cpp`, the renderer of every OpenGL waveform
  type and thus the default, draws the bar line at the alpha of the skin and
  the beat line at 60 percent of it.
- `renderers/waveformrenderbeat.cpp`, the QPainter renderer of the Software,
  HSV and RGB waveform types, draws the bar line twice as wide. It cannot use
  alpha: `drawLines` with an alpha under 1 paints one large rectangle on the
  QOpenGLWindow, which is why that renderer forces the alpha to 1.

A track with no bar phase looks as it did before in both renderers.
The overview does not draw bar lines.

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

`beat_in_bar` is also in the **default** publish list, thus a new profile
sends `/mixxx/ChannelN/beat_in_bar` as a float on each change. Mixxx writes
`osc-publish.conf` only when the file is absent, thus a rig that already ran
this fork keeps its old list. Add this line to
`~/.mixxx/osc-publish.conf` by hand, under the other `[ChannelN]` lines:

```
[ChannelN] beat_in_bar
```

The beat message carries the bar in any case, because it does not come from
the publish list.

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

- No bar quantization. A loop on the bar, a beat jump by bars and a cue
  quantized to the bar are out of scope. The stored phase is what those
  features would need.
- No time signature other than 4/4. Nothing writes `beats_per_bar` with
  another value.
- No downbeat in the waveform overview and no downbeat in the library.
- No reference set. Four tracks are measured against two energy features,
  and no person marked the bars of a real track.
- The external detector reads the labels of the program as 4/4. A program
  that reports a bar of three or seven beats gives a scattered histogram,
  and the track then gets no phase.
- No progress for the external command. The analyzer shows the progress of
  the file, and the command runs at the end of it.
- The external command runs one track at a time, in each analyzer thread.
  Nothing batches the tracks into one call of the program, which a card
  would like better.
