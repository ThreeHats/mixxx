# The muxic fork

This fork carries five features for the muxic rig. The plan is in the muxic
repository, in `docs/mixxx-fork.md`. This fork is for private use. It sends
no pull request, no issue and no comment to `mixxxdj/mixxx`.

## Branches

| Branch | Content |
|---|---|
| `main` | A copy of upstream `main`. No fork commits |
| `muxic` | The integration branch. The rig runs a build of this branch |
| `muxic-stems` | Stem conversion in the library |
| `muxic-related-tracks` | Related tracks |
| `muxic-library-window` | Floating library window |
| `muxic-osc` | OSC state server |
| `muxic-library-columns` | More library columns |

Each feature branch starts from `muxic` and merges back into it. To take a
new upstream version, merge `upstream/main` into `muxic`.

## Where the fork code is

| Feature | Code | Page |
|---|---|---|
| Stem conversion | `src/stems/`, `src/preferences/dialog/dlgprefstems.*` | `docs/stems.md` |
| Related tracks | `src/muxic/relatedtracks/` | `docs/related-tracks.md` |
| Floating library window | `src/librarywindow/` | `docs/library-window.md` |
| OSC state server | `src/osc/`, `src/preferences/dialog/dlgprefosc.*` | `docs/osc.md` |
| More library columns | `src/muxic/librarycolumns/` | `docs/library-columns.md` |
| Downbeat detection | `src/muxic/downbeats/`, `src/track/beats.*`, `src/util/commandtemplate.*` | `docs/downbeats.md` |
| Related tracks panel | `src/muxic/relatedtracks/` | `docs/related-tracks.md` |
| Small fixes | `src/widget/wlibrarytableview.cpp`, header restore | `docs/small-fixes.md` |

`src/stems/`, `src/osc/` and `src/librarywindow/` are subsystems with a CMake
option or a place of their own in the Mixxx layout. `src/muxic/` holds the
library data that only this fork has, one directory for each feature. The
tests are in `src/test/`. To see each line that the fork changed in an
upstream file, run `git diff upstream/main...muxic --stat`.

The fork tables are `muxic_track_relations` and `muxic_track_meta`. The
muxic hub reads and writes the two tables. Each page gives the exact schema
and the rules for the hub.

## Tests

`TrackMetadataExportTest.keepWithespaceKey` fails in the container on
upstream `main` too. A controller script test leaves a key notation in a
global state, and this test reads it. All other tests must pass.

## Build

The build runs in a container, thus the host needs only Docker. The image is
Ubuntu 22.04, the same base as the rig.

```
tools/muxic/build.sh image       # make the image, one time
tools/muxic/build.sh configure   # cmake, into ./build
tools/muxic/build.sh build       # compile
tools/muxic/build.sh test        # run mixxx-test, gtest arguments pass through
tools/muxic/build.sh run <cmd>   # run a command in the container
```

- `build` and `test` take one of two build slots. A third caller waits. This
  keeps parallel worktrees in the memory of the machine.
- `MUXIC_JOBS` sets the compile jobs of one build. The default is 6.
- All worktrees share one ccache in `~/.cache/mixxx-muxic-ccache`.
- The container has no sound device. It has `xvfb-run`, `xdotool` and
  `import` for a screen test.

The home directory in the container is `/tmp`, thus a test run cannot touch
the `~/.mixxx` of the rig. On the host, start a fork build only with
`--settings-path` and a scratch directory, until the rig moves to the fork.

## Rules for fork code

1. Keep the diff to upstream files small. Put new code in new files, and
   connect it to upstream code at a small number of lines. A small diff makes
   each upstream merge cheap.
2. Do not add a revision to `res/schema.xml`. Upstream owns the revision
   numbers. A fork table has the prefix `muxic_`, and its DAO makes it with
   `CREATE TABLE IF NOT EXISTS` when the database opens. Key a fork table on
   `library.id`.
3. Do not change the columns of `library`, `track_locations` or `cues`. The
   muxic hub reads and writes them.
4. Obey the Mixxx code style in `CONTRIBUTING.md`. Format changed lines with
   `git clang-format`.
5. The engine thread does no allocation, no lock and no I/O.
6. Each new unit of logic has a test in `src/test/`.
7. Put each string that the user sees in `tr()`.
8. Prose obeys ASD-STE100: comments, commit messages and documents. A comment
   is rare and has at most two lines.
9. Each feature has one page in `tools/muxic/docs/`.
