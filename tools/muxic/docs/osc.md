# The OSC state server

This fork sends the state of Mixxx as Open Sound Control (OSC) messages over
UDP, and it takes OSC commands back. It replaces the controller script
`mixxx/Local_Clock_Relay.js` of the muxic rig. The messages carry full
precision, they carry text, and a change of the settings needs no restart of
Mixxx.

The code is in `src/osc/`. The CMake option `OSC` builds it. The option is on
when `pkg-config` finds `liblo`. With `-DOSC=OFF` the build has no OSC code.

## Turn it on

1. Open Preferences, then OSC.
2. Select **Enable the OSC state server**.
3. Set the bind address and the port. The default is `127.0.0.1` and `9000`.
4. Add each reader under **Targets**, as `host:port`, separated by commas. A
   reader that speaks to Mixxx can also ask for the state by itself. See
   **Subscribe**.
5. Push Apply. The server starts. A restart of Mixxx is not necessary.

Two controls also hold the switch, thus a controller mapping or a script can
reach it:

| Control | What it does |
|---|---|
| `[Osc] enabled` | Starts and stops the server. It writes the setting too |
| `[Osc] reload` | Reads the settings and the publish list again |

## The address space

The address of a control is the group without its brackets, then the key:

```
/mixxx/<group>/<key>
```

`[Channel1]` and `play` give `/mixxx/Channel1/play`. `[Master]` and
`crossfader` give `/mixxx/Master/crossfader`. The map works in both
directions, thus an address that comes in names the control that it writes.

A control message carries one `f` (32-bit float) argument. It holds the value
of the control, in the units of Mixxx.

The module drops a group or a key that holds a character which OSC keeps for
its patterns (space, `#`, `*`, `,`, `/`, `?`, `[`, `]`, `{`, `}`).

### What the module sends by default

`[ChannelN]` stands for each deck.

| Address | Type | Meaning |
|---|---|---|
| `/mixxx/ChannelN/play` | f | 1 while the deck plays |
| `/mixxx/ChannelN/track_loaded` | f | 1 while the deck holds a track |
| `/mixxx/ChannelN/bpm` | f | The rate that the listener hears, with the speed slider |
| `/mixxx/ChannelN/file_bpm` | f | The rate of the beat grid of the file |
| `/mixxx/ChannelN/rate` | f | The speed slider, -1 to +1 |
| `/mixxx/ChannelN/beat_distance` | f | The part of the beat that is over, 0 to 1 |
| `/mixxx/ChannelN/volume` | f | The channel fader, 0 to 1 |
| `/mixxx/ChannelN/mute` | f | 1 while the channel is mute |
| `/mixxx/ChannelN/pregain` | f | The gain knob |
| `/mixxx/ChannelN/orientation` | f | 0 left, 1 center, 2 right |
| `/mixxx/ChannelN/key` | f | The key of the track, Mixxx ChromaticKey 0 to 24 |
| `/mixxx/ChannelN/key_text` | s | The name of the key, in the notation of the user |
| `/mixxx/ChannelN/duration` | f | The length of the track, in seconds |
| `/mixxx/ChannelN/playposition` | f | The position, 0 to 1 |
| `/mixxx/ChannelN/sync_leader` | f | 1 while this deck is the sync leader |
| `/mixxx/ChannelN/sync_enabled` | f | 1 while sync is on |
| `/mixxx/Master/crossfader` | f | -1 hard left, 0 center, +1 hard right |
| `/mixxx/Master/headMix` | f | -1 cue only, +1 main only |
| `/mixxx/Master/headGain` | f | The level of the headphones, in dB |
| `/mixxx/Master/duckStrength` | f | The mailbox that the Mixtrack mapping writes |

`key_text` goes out with each `key` message. It is not in the publish list.

Mixxx does not know which deck the audience hears. The relay computes that
from `play`, `volume`, `mute`, `orientation` and `crossfader`, as the
controller script did.

### Track metadata

Each of these goes out when a deck loads a track. An empty deck gets an empty
string in each of them.

| Address | Type |
|---|---|
| `/mixxx/ChannelN/artist` | s |
| `/mixxx/ChannelN/title` | s |
| `/mixxx/ChannelN/album` | s |
| `/mixxx/ChannelN/year` | s |
| `/mixxx/ChannelN/genre` | s |
| `/mixxx/ChannelN/location` | s |

The length of the track is the control `duration`, in seconds.

## The beat message

```
/mixxx/ChannelN/beat   h  i  f  i
```

| Argument | Type | Meaning |
|---|---|---|
| 1 | h (int64) | The instant of the beat, in nanoseconds on CLOCK_MONOTONIC |
| 2 | i (int32) | The place of the beat in the beat grid of the track |
| 3 | f (float) | The rate that the listener hears, in beats per minute |
| 4 | i (int32) | A counter of the beats that this deck sent |

**The beat count.** Argument 2 counts from the anchor beat of the beat grid.
For a track with one tempo, the anchor is the first downbeat of the grid, thus
`beat % 4 == 0` marks a bar. For a track with tempo markers, the anchor is the
first marker. A beat before the anchor gets a negative number. This count does
not restart at a play press, and a loop moves it back. Argument 4 only rises,
thus a reader sees at once that a beat was lost.

**The clock.** The stamp is `std::chrono::steady_clock`. On Linux with glibc
that clock is `clock_gettime(CLOCK_MONOTONIC)`. Node gets the same clock from
`process.hrtime.bigint()`, and the time base of `performance.now()` in Chrome
is the same clock. Thus the relay and the browser can compare a stamp with
their own clock without an offset. Do not compare it with `Date.now()`,
because NTP moves that clock.

**What the stamp means.** It is the instant at which the beat leaves the
outputs of the sound card, not the instant at which Mixxx computed it. The
engine takes it from three numbers:

1. The start of the audio callback, which `VisualPlayPosition` holds.
2. The time from that start until the first frame of the buffer reaches the
   digital-to-analog converter (DAC). PortAudio gives
   `outputBufferDacTime`, and PipeWire gives `clock.delay`. Mixxx already
   measures this for the waveform.
3. The place of the beat inside the buffer. The engine knows the frame of the
   beat and the frames that the buffer carries.

**The error.** About 1 ms, and not more than one buffer period.

| Source | Size |
|---|---|
| The DAC time of the sound API | Below 1 ms with a good driver. Mixxx falls back to a CPU estimate when the API reports a time that does not add up, and then the error can reach one buffer period (21 ms at 1024 frames and 48 kHz) |
| The rate inside one buffer is taken as constant | Below one frame, about 20 µs at 48 kHz |
| Two reads of the clock in the same callback | Below 1 µs |

**When a deck sends no beat.** The deck sends a beat only while it plays
forward at a rate above 0.01, and only when the beat falls inside the buffer
that the engine has just made. A seek, a loop end or a scratch can move the
position over a beat. That beat gets no message, and the next one comes as
usual. A track without a beat grid sends no beat.

**The path inside Mixxx.** The engine thread writes each beat into a
lock-free queue of 64 places (`src/osc/oscbeatfeed.cpp`). It allocates
nothing, it locks nothing and it reads no file. The OSC thread empties the
queue every 5 ms. Thus a beat message leaves up to 5 ms after its beat, but
the instant inside the message does not move.

## Commands in

A message to the address of a control writes that control:

```
/mixxx/Channel1/play 1.0
```

The first argument can be `f`, `d`, `i`, `h`, `T` or `F`. The module takes it
as a number.

By default the module writes only the transport controls. The allow list
holds these keys, where `*` stands for any text:

```
play, play_stutter, start_play, start_stop, stop,
cue_default, cue_gotoandplay, cue_gotoandstop, cue_set,
beatjump, beatjump_size, beatjump_forward, beatjump_backward, beatjump_*,
beatloop_*, beatlooproll_*,
loop_in, loop_out, loop_double, loop_halve, reloop_toggle, reloop_andstop,
hotcue_*,
sync_enabled, sync_leader, sync_mode, quantize, keylock,
rate, rate_temp_up, rate_temp_down, rate_perm_up, rate_perm_down
```

**Let a message write any control** in the preferences removes the list. A
network port that writes any control of Mixxx is a risk. Read **Security**
before you select it.

To set your own list, put a comma-separated list of globs in
`[Osc] AllowedKeys` in `mixxx.cfg`. An empty setting keeps the list above.

## Snapshot and subscribe

A reader that starts after Mixxx needs the whole state. Three ways give it:

| Address | What happens |
|---|---|
| `/mixxx/snapshot` | Mixxx sends the state to the sender of the message |
| `/mixxx/subscribe` | Mixxx sends the state to the sender, then keeps it as a target for 60 seconds |
| `/mixxx/unsubscribe` | Mixxx drops the sender from its targets |

`/mixxx/subscribe` takes one number argument. It is the port to send to. With
no argument, Mixxx answers to the port that the message came from.

A reader must send `/mixxx/subscribe` again before the 60 seconds are over.
Every 20 seconds is a good period.

A snapshot is one message per control and per metadata field, then:

```
/mixxx/snapshot_end   i
```

The argument counts the messages of the snapshot. A reader can wait for this
address before it shows the state.

The setting **Snapshot every** sends the state to each target on a timer. 0
stops the timer.

## The publish list

The file `osc-publish.conf` in the settings directory lists the controls that
go out. Mixxx writes the file with its defaults when the file is not there.
One line is one control:

```
<group> <key> [shortest time between two messages, in ms]
```

`[ChannelN]` stands for each deck. A `#` starts a comment. A line without an
interval sends each change at once. An interval holds a fast control back, and
the last value always goes out.

Example:

```
[ChannelN] play
[ChannelN] playposition 100
[Master] crossfader 30
```

Push Apply in the preferences, or set `[Osc] reload`, to read the file again.

## The settings

`mixxx.cfg` holds these under `[Osc]`.

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | 0 | Starts the server |
| `ListenHost` | `127.0.0.1` | The address that the socket binds to |
| `ListenPort` | 9000 | The port that the socket binds to |
| `Targets` | empty | `host:port` of each reader, separated by commas |
| `SnapshotIntervalSeconds` | 10 | The period of the snapshot timer. 0 stops it |
| `AllowAllControls` | 0 | 1 lets a message write any control |
| `AllowedKeys` | empty | Globs for the allow list. Empty keeps the built-in list |

An IPv6 target needs brackets, for example `[::1]:9001`.

## Security

- The socket binds to `127.0.0.1`. A program on another computer cannot reach
  it. To take messages from the network, change `ListenHost`.
- The server has no password and no encryption. OSC over UDP carries neither.
  Keep the bind address on the loopback, or put the port behind a firewall.
- The allow list stops a message from writing a control that is not transport.
  Keep **Let a message write any control** off unless you trust every program
  that can reach the port.
- A reader that subscribes gets the state of Mixxx, which holds the artist,
  the title and the file path of each loaded track.

## Migration from Local_Clock_Relay.js

The controller script sends 7-bit MIDI CC messages on channel 16, and
`relay/bpm_midi_bridge.js` reads them. This table maps each CC to its OSC
address. The OSC side carries the full precision, thus the pairs of CCs and
the 14-bit packing go away.

| CC | OSC address | Notes |
|---|---|---|
| 81, 82 | `/mixxx/ChannelN/bpm` | One float, no coarse and fine pair, no 60 to 187 limit |
| 83 | `/mixxx/ChannelN/beat` | Argument 2 is the beat count, argument 4 is the sequence number |
| 84, 85 | `/mixxx/ChannelN/beat`, argument 1 | The stamp takes the place of `beat_distance`. The relay does not correct the pulse any more. `beat_distance` is still on its own address for a meter |
| 86 | none | The relay picks the audible deck from `play`, `volume`, `mute`, `orientation` and `crossfader` |
| 87 | `/mixxx/Master/duckStrength` | The packed mailbox of the Mixtrack mapping. The relay unpacks it as before |
| 88 | `/mixxx/ChannelN/key` | Also `/mixxx/ChannelN/key_text` as a string |
| 89 | `/mixxx/Master/duckStrength` | The same mailbox as CC 87 |
| 90 to 93 | `/mixxx/ChannelN/volume` and `/mixxx/ChannelN/mute` | Two addresses, not one packed value |
| 94 | `/mixxx/Master/crossfader` | -1 to +1, not 0 to 127 |
| 95 to 98 | `/mixxx/ChannelN/orientation` | |
| 99 | `/mixxx/ChannelN/play` | One address per deck, not a bit mask |
| 102 to 109 | `/mixxx/ChannelN/duration` | Seconds as a float. The relay does not need it to tell the decks apart any more, because `/mixxx/ChannelN/location` gives the file |
| 110, 111 | `/mixxx/ChannelN/play`, `/mixxx/ChannelN/beatjump` | A command writes the control by its own address. Deck 0 of the old protocol is gone, thus the relay names the deck |

Three limits of the old path go away:

- A change of the script needed a restart of Mixxx. A change of the settings
  or of `osc-publish.conf` needs only Apply.
- MIDI carries no text. OBS gets the artist and the title from
  `/mixxx/ChannelN/artist` and `/mixxx/ChannelN/title`.
- The bridge stamped a beat when the MIDI message arrived, which is early by
  the output latency of Mixxx and late by up to one buffer. The relay took
  that back with `beat_distance` and the `LATENCY` message. The OSC stamp
  needs neither.

The rig can run both paths at the same time. Keep `Local_Clock_Relay.js` in
service until the OSC path holds the beat as well.

## What is not done

- The module reads a plain OSC message. It does not read an OSC bundle.
- A snapshot is one datagram per message. It is not one bundle.
- The module has no OSC query protocol, thus a reader cannot ask for the list
  of addresses.
- The publish list has no editor in the preferences. It is a text file.
- The beat message covers a deck that plays forward. A scratch and a reverse
  send no beat.
