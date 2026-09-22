# Stem conversion

Mixxx plays a `.stem.mp4` file with one fader per stem. This page tells how
the fork makes such a file from a track of the library, and how the new track
gets the cues, the grid and the tags of the source track.

## What it does

1. You select one track or many tracks in the library.
2. The context menu has **Stems > Convert to Stems**.
3. A queue runs one conversion at a time. Each conversion:
   - runs the separation command, which writes four audio files,
   - runs the encode command one time per audio stream,
   - runs MP4Box two times: one time to mux the five streams, one time to
     write the stem manifest in the `moov/udta/stem` atom and the cover
     image of the source file,
   - writes the tags of the source track into the stem file with TagLib.
4. The new file goes to the library through the normal track API. Mixxx
   reads the tags of the new file at that moment.
5. The new track gets the cues, the loops, the grid, the key, the BPM lock,
   the rating, the color, the comment and the ReplayGain of the source
   track.

The stem file holds five stereo streams: the source mix, then Drums, Bass,
Other and Vocals. This order and this count are what the stem reader of Mixxx
wants. A different order or a different count makes the file unreadable.

Each external program runs in QProcess and each file step, which means the
cover image, the tag write, the check of the manifest and the move into the
music folder, runs in a worker thread. The GUI thread and the engine thread
thus stay free, and Mixxx plays audio without a dropout while a model runs.

## What you install on the host

Mixxx installs nothing. It runs the programs that you name in the
preferences. You install them yourself:

| Program | Job | How to install |
|---|---|---|
| `demucs` | Separation | `pipx install demucs`, or `pip install --user demucs` |
| `audio-separator` | Separation, other option | `pipx install "audio-separator[gpu]"` |
| `ffmpeg` | Encode | `sudo apt install ffmpeg` |
| `MP4Box` | Mux and manifest | `sudo apt install gpac` |

The first run of `demucs` downloads the model, about 300 MB. The rig has two
NVIDIA cards. `CUDA_VISIBLE_DEVICES` in the command selects one of them.

## Preferences

The page is **Preferences > Stems**. The values are in `mixxx.cfg` under the
group `[Stems]`.

| Item | Default | Meaning |
|---|---|---|
| `SeparatorCommand` | `demucs -n $MODEL -o $OUTPUT_DIR "$INPUT"` | The command that writes the four stem files |
| `Model` | `htdemucs` | The value of `$MODEL` |
| `EncoderCommand` | `ffmpeg -hide_banner -nostdin -y -i "$INPUT" -ar $SAMPLE_RATE -c:a aac -b:a 256k "$OUTPUT"` | The command that recodes one stream. An empty value skips the recode |
| `MuxerPath` | `MP4Box` | The program that muxes and writes the manifest |
| `OutputMode` | `source` | `source` writes next to the source file. `custom` writes in `OutputDirectory` |
| `OutputDirectory` | empty | The directory for `custom` |

### Placeholders

The separation command knows `$INPUT`, `$OUTPUT_DIR` and `$MODEL`. The encode
command knows `$INPUT`, `$OUTPUT` and `$SAMPLE_RATE`. Both forms `$NAME` and
`${NAME}` work. A quote holds a value with a space together, thus
`"$INPUT"` stays one argument. A name in any case matches, thus `$input`
and `$Model` are errors and not plain text. A name that is not in the list
above, and a name whose value is empty, stop the job with a message that
names it.

### Command templates to paste

demucs on the first card:

```
env CUDA_VISIBLE_DEVICES=0 demucs -n $MODEL --shifts 1 -o $OUTPUT_DIR "$INPUT"
```

demucs on the second card, with the fine-tuned model:

```
env CUDA_VISIBLE_DEVICES=1 demucs -n htdemucs_ft -o $OUTPUT_DIR "$INPUT"
```

audio-separator with a Demucs model:

```
audio-separator "$INPUT" --output_dir $OUTPUT_DIR --model_filename $MODEL
```

With `audio-separator`, set `Model` to a model file name, for example
`htdemucs_6s.yaml`. A UVR model runs the same way, because `audio-separator`
loads the UVR model files.

The job reads the four files back by name. A file name must hold the word
`drums`, `bass`, `other` or `vocals`. The base name of the source drops out
of each name first, and the text of the last parentheses wins, thus
`Drum and Bass Anthem_(Bass)_htdemucs.wav` goes to Bass and not to Drums. A
name that still holds the word of more than one stem is skipped. Two files
for one stem stop the job. A two-stem run stops the job too, because it
writes no drum file.

Lossless output, with no recode and no resample:

```
EncoderCommand =
```

An empty encode command puts the WAV files in the MP4 as raw PCM. The file
is then about ten times larger than an AAC file.

## The sample rate

A cue position and a beat marker are frame counts at the sample rate of their
own file. `htdemucs` always writes 44100 Hz. A 48 kHz source with no resample
thus gives a stem file with a different frame count for the same second.

The decision: **the default encode command resamples to the sample rate of
the source track** (`-ar $SAMPLE_RATE`). The frames then map one to one.

The copy step is safe in both cases. A cue goes to the new track through the
millisecond form of `CueInfo`, and the grid gets new markers at the scaled
positions. A second stays a second. If the two rates differ, the job writes a
message in the conversion window that names both rates. Frame rounding can
move a position by less than one sample.

## The encoder delay

An AAC encoder puts priming samples, 1024 or more, at the head of the stream.
If the reader plays them, every cue lands late by that count. The test
`JobEncodesWithTheDefaultTemplateAndKeepsTheFrames` measures it: a click at
frame 48000 of a 48 kHz source, four separated files at 44100 Hz, the default
encode command, then the stem reader of Mixxx. **The click comes back at
frame 48000. The offset is 0 frames.**

The reason: ffmpeg writes an edit list (`elst`) that trims the priming
samples, MP4Box keeps that edit list through the mux, and libavformat gives
the trim to the decoder. AAC thus stays the default. You need no ALAC for the
alignment. ALAC (`-c:a alac`) is still a good option for a lossless stem
file, and a stem player accepts it, but the file is about four times larger.

## Where the job writes

The muxer, the manifest and the tags all work on a file in a temporary work
directory. The last step moves that file into the music folder: first to a
hidden name that ends with `.part`, then a rename to the final name. A
library scan, beets or the hub thus never see a half written stem file, and a
stop of Mixxx in the middle leaves nothing but the work directory.

The job removes only the files that it made: the work directory and its own
`.part` file. It never removes the stem file of an earlier run. A conversion
that finds its output file in place stops with a message.

## The conversion window

**Stems > Show Conversions...** opens the window. It lists each job with the
track, the state, the progress and the message. The states are Queued,
Preparing, Separating, Reading the stem files, Encoding, Muxing, Writing the
stem manifest, Writing the tags, Done, Failed and Cancelled. The buttons
cancel the selected jobs, cancel all jobs, or drop the jobs that ended.

The queue runs one job at a time, because a separation model fills the memory
of the graphics card.

A failed job keeps the last 4000 characters of the output of the program in
its message, thus you see why the model or the muxer stopped. Both output
channels of the program go into that text.

## The stem manifest

The `moov/udta/stem` atom holds this JSON:

```json
{"version":1,
 "mastering_dsp":{"compressor":{"enabled":false},"limiter":{"enabled":false}},
 "stems":[{"name":"Drums","color":"#009E73"},
          {"name":"Bass","color":"#D55E00"},
          {"name":"Other","color":"#CC79A7"},
          {"name":"Vocals","color":"#56B4E9"}]}
```

The colors are the colors that Mixxx gives to a stem file with no colors,
thus a converted file looks like the rest of the library.

## The tags and the cover image

The stem file carries its own tags, thus beets, the organizer and a
re-import of the library read the artist and the title with no help from the
Mixxx database.

- MP4Box writes the cover image of the source file into `moov/udta/meta/ilst`
  in the same call as the stem manifest. A source with no image gets no
  image.
- TagLib writes the tag fields of the source track (title, artist, album,
  album artist, composer, grouping, genre, year, track number, comment, BPM,
  key, ReplayGain) through the normal Mixxx tag writer,
  `MetadataSourceTagLib`, which is the same code that the **Export metadata
  into file tags** action uses.

TagLib keeps the `stem` atom and the cover image when it writes the tags. The
test `JobWritesTheTagsAndKeepsTheStemManifest` proves both: after the tag
write, `StemInfoImporter` still reads four stems and the cover image reads
back.

## The size of the waveform

A stem file holds four parts that sum to the mix, thus each part alone is
much quieter than the mix. Mixxx draws a stem track with a renderer of its
own, `WaveformRendererStem`, which reads the four stem bands of the waveform
and lays them on top of each other. A file with no stems gets one of the
other signal renderers, which read the all band or the low, mid and high
bands. The all band holds the mix. A stem track thus looked much smaller
than the same music in a file with no stems.

The measurement, with four parts at 40, 30, 20 and 10 percent of the mix:
the all band reaches 250 and the loudest stem band reaches 100, in the
source file and in the stem file. On a waveform of 80 pixels the stem track
drew 15.7 pixels where the normal track drew 39.2 pixels.

The fork lifts the parts in each strip of the waveform: the factor is `all`
over the loudest part of that strip. The outline of a stem track is then the
outline of the mix, which is what you read while you mix, and the parts keep
their size relative to each other inside the strip. The cost: a part changes
height when another part starts or stops, because the lift follows the
loudest part. One factor for the whole track was tried first. On a real track
the peak of one part comes close to the peak of the mix (a drum hit), thus
that factor was 1.05 while the strips needed 1.6 on average, and the waveform
stayed at 66 percent. The code is in
`src/waveform/renderers/stemwaveformscale.h` and
`src/waveform/renderers/allshader/waveformrendererstem.cpp`.

The same change removed a second fault of the renderer: the gain of the deck
came into the height two times, thus a track with a ReplayGain of -3 dB drew
41 percent too big and ran past the edge of the waveform.

- All waveform types use this one renderer for a stem track, thus every
  item of **Preferences > Waveforms > Waveform type** gets the fix: Simple,
  Filtered, HSV, RGB and Stacked. Simple, HSV and RGB draw a normal track at
  the height of the all band, thus a stem track and a normal track now have
  one height with those three. Filtered and Stacked draw the low, the mid
  and the high band one over the other, and the tallest of the three is at
  most the all band, thus a stem track can look a little taller than a
  normal track with those two.
- The overview needs no change. It reads the all band.
- The cause is in Mixxx and not in the conversion. The sample stem file of
  Mixxx, `src/test/stems/sin_AAC_256kbps_VBR.stem.mp4`, has the same shape:
  the mix stream peaks at +1.0 dB and each stem stream at -3.2 dB. The owner
  can report the change to Mixxx.
- The ReplayGain is not the cause of the small waveform. Read the next
  section for what the ReplayGain does to the height.

### The display mode of a stem track

**Preferences > Waveforms > Display mode** selects how a stem track looks.
This item is not the **Waveform type** item, which selects the colors of
every track.

| Display mode | What it does |
|---|---|
| Overlapping | The default. The four parts lie on top of each other, and the factor above lifts them to the height of the mix |
| Stacked | Each part gets a lane of a quarter of the height. Mixxx draws each lane at the level of that part, with **no factor**, because the reason to use this mode is to read the level of one part |

The fork changed the Overlapping mode only. The Stacked mode looks as it
did.

## The ReplayGain of a stem file

The work on the waveform found a second bug of Mixxx. The EBU R128
analyzer, which computes ReplayGain 2.0, gave the eight channels of a stem
source to libebur128 as they are. libebur128 reads more than two channels as
a surround signal, thus it read the drums as left and right, the left bass
channel as the center, the other channels as the two surround speakers with
a weight of 1.41, and it dropped the vocals and the right bass channel.

The measured error of a test file is **5.83 dB**. A stem file that Mixxx
analyzes itself thus plays too loud and draws a waveform that is too big,
because the visual gain of a waveform follows `total_gain`.

The fork mixes the stem channels down to stereo before the analyzer, in the
same way as the ReplayGain 1.0 analyzer already does. The code is in
`src/analyzer/analyzerebur128.cpp`.

Which files this hits:

- A stem file from another source, for example a file that you buy.
- A stem file of a source track that carries **no** ReplayGain. The job then
  copies an empty gain, and the analyzer runs on the stem file.
- Any stem track on which you start **Re-analyze**, and any stem track that
  an earlier build of the fork analyzed. Such a track carries a ReplayGain
  that is up to 5.8 dB too high, it plays too loud and it draws a waveform
  that is too big. Run **Re-analyze** on those tracks with this build to
  correct them.

A stem file of a source track that carries a ReplayGain is not affected. The
job copies that gain and the analyzer then does not run.

This is a bug of Mixxx, thus the owner can report the change.

## How the muxic rig sees it

The fork adds no table and no column. The new stem file is a normal row in
`library` and normal rows in `cues`, thus the hub tools
(`hub/muxichub/mixxxdb.py`, the dedupe tool, the long-track tool) read it with
no change. The hub sees the source track and the stem track as two tracks
with the same tags.

## What is not done

- No watch folder. You start each conversion from the library.
- No progress from the muxer. The percentage during separation comes from the
  output of the separation program. A program that prints no percentage jumps
  from 5 % to 70 %.
- Two conversions of one track at the same time are refused. The second one
  fails with a message, because both would write one file.
- No delete of the source track. The library holds both tracks.
- A cover image that lives in a file next to the track, and not in the tags,
  does not go into the stem file. MP4Box gets only the image that TagLib
  reads out of the source file.
- The ReplayGain of the source is copied as is. Mixxx mixes the four stems on
  the fly and applies no DSP, thus the loudness of the mix can differ a little
  from the loudness of the source master.
- No test with a real model, because the build image has no `demucs`. The
  test uses a shell script for the separation step, and the real `ffmpeg` and
  `MP4Box` for the rest.
