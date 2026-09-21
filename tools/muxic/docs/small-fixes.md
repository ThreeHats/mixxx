# Small fixes of the library

Two fixes that the owner asked for after the first use of the fork on the
rig. They belong to no feature of their own.

## Shift and the wheel move a table to the side

Hold `Shift` and turn the wheel over a library table. The table moves to the
side. The step is the step of a tilt wheel with no `Shift`, thus a table with
many columns moves the same distance for both ways to scroll.

The fix is in `WLibraryTableView`, the base class of each table of the
library, thus each table gets it:

| Table | Class |
|---|---|
| Tracks, a playlist, a crate, the history, Computer | `WTrackTableView` |
| Auto DJ, Recordings, Hidden Tracks, Missing Tracks | `WTrackTableView` |
| Analyze | `WAnalysisLibraryTableView` |

Qt gives a wheel with `Shift` to the vertical bar, which then moves by a
page. The fork makes a wheel to the side of such an event, drops `Shift` and
`Ctrl`, and gives it to the horizontal bar. `Ctrl` goes away too, because it
also makes a bar move by a page. A wheel that already goes to the side keeps
the way of Qt. A wheel with no `Shift` moves the table up and down as before.

A table with no space to the side leaves the event free. A widget above the
table can then take it. The table itself does not move up and down in that
case.

The page `library-columns.md` gives the columns that the fork adds.

## The log at the start

`WTrackTableViewHeader` restored a saved layout of the columns and put one
warning in the log for each column that the layout names and the view does
not have:

```
Warning [Main]: Header view: skipping restore for invalid column index -1
```

The fork adds columns, thus a layout that an older build saved names
columns that a view of the fork does not have. The line came seven times at
each start on the rig. The restore now counts such columns and logs one
debug line:

```
Debug [Main]: Header view: the saved state has 7 columns that this view does not have
```

## Code

| File | Content |
|---|---|
| `src/widget/wlibrarytableview.cpp` | `wheelEvent()`: the wheel to the side |
| `src/widget/wtracktableviewheader.cpp` | The count of the skipped columns |
| `src/test/wlibrarytableview_test.cpp` | 6 tests on a table with wide columns. Five send a wheel event, one shows that the table can move both ways |

## What is not done

- The sidebar of the library is a tree, not a table. There `Shift` and the
  wheel keep the way of Qt, which moves the tree up or down by a page.
- The step comes from Qt (`QApplication::wheelScrollLines()`), thus it is
  not a setting of Mixxx.
- A touchpad gives frames with a scroll phase. A frame that comes with no
  angle, such as the frame that starts or ends a gesture, keeps the way of
  Qt. A device that gives only pixels and no angle thus gets no move to the
  side from `Shift`.
