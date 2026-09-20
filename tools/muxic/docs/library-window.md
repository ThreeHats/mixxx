# The floating library window

The library of Mixxx can move to a window of its own. Put that window on the
second monitor, and the main window keeps the decks, the waveforms, the mixer
and the effects. The main window gives the free space to the waveforms.

The window holds the full library area of the skin: the sidebar, the search
box, the track table, the preview deck and the cover art, because the fork
moves the widget that the skin builds for the library.

## How to use it

| Action | Result |
|---|---|
| View menu, "Library in a Window of its Own" | The library moves out, or comes back |
| `Ctrl+7` | The same toggle, from either window |
| `Ctrl+W` in the library window | The library comes back to the main window |
| The close button of the window manager | The library comes back to the main window |

The window never keeps the library for itself. Each way to close it puts the
library back in the main window.

The state persists. If the library was in its own window at the last exit,
Mixxx opens that window again at the next start, with the same size, the same
position and the same screen.

## The control

| Control | Type | Default | Use |
|---|---|---|---|
| `[Skin],show_library_window` | toggle button, persistent | 0 | 1 puts the library in its own window |

A controller mapping can bind the control. The control is in the `[Skin]`
group with `show_maximized_library` and the other view controls, and Mixxx
makes it one time at the start, thus it works with each skin.

The menu entry reads the same control, thus the check mark follows a change
that comes from a controller.

## The config keys

Mixxx writes these keys to `mixxx.cfg`.

| Key | Content |
|---|---|
| `[Skin] show_library_window` | The state of the toggle, 0 or 1 |
| `[LibraryWindow] geometry` | `x,y,width,height` of the window |
| `[LibraryWindow] screen` | The name of the screen that holds the window |
| `[LibraryWindow] state` | `normal`, `maximized` or `fullscreen` |

At the start, Mixxx looks for the saved screen. If that screen is absent, the
window goes to the center of the primary screen with the saved size. If the
saved area is larger than the screen, or outside of it, Mixxx moves the window
into the screen.

## What works while the library is detached

- The keyboard shortcuts of the menu bar, because Mixxx gives each one the
  application context. `F11` in the library window makes the main window full
  screen.
- The keyboard mapping of Mixxx (`res/keyboard/*.kbd.cfg`), because the
  window has the same keyboard event filter as the main window.
- The library controls of a controller (`[Library],MoveFocus`, `GoToItem`,
  `MoveVertical` and the others). A control that asks for the focus now makes
  the window of the library widget the active window first.
- `[Library],focused_widget`, because Mixxx follows the focus of each window.
- Drag and drop of a track from the track table to a deck of the main window.
- The track context menu, the tooltips and the skin style. The window takes
  the style sheet and the colors of the main window and of the skin.
- A skin reload and a skin change. Mixxx puts the library back in the old skin
  first, then takes the library area of the new skin and opens the window
  again.
- The "Maximize Library" toggle of the main window.

## The skins

The mechanism is in C++ and needs no change to a skin. Each skin of Mixxx
builds the library one time as a singleton and shows it through one or more
singleton containers. The fork finds the singleton that holds the `WLibrary`
widget, moves it to the window, and hides the containers so that the main
window gives the space to the other widgets.

| Skin | State |
|---|---|
| LateNight, LateNight (64 Samplers) | Works. Checked with screenshots |
| Deere, Deere (64 Samplers) | Works. Checked with screenshots |
| Tango, Tango (64 Samplers) | Works, from the same singleton mechanism. Not checked with screenshots |
| Shade | Works, from the same singleton mechanism. Not checked with screenshots |
| LateNightQML | No. The QML skin is out of scope |

A skin that has no library singleton gets a second mechanism: the fork takes
the closest widget that holds the track table, the sidebar and the search box,
and puts a stand-in of zero size in its place. This needs a parent with a
layout. If the skin gives neither, the log says that the skin has no library
area for a window of its own, and the toggle does nothing.

## Code

| File | Content |
|---|---|
| `src/librarywindow/librarywindowmanager.cpp` | Finds the library area, moves it, and follows the control |
| `src/librarywindow/wlibrarywindow.cpp` | The window: style, geometry, close |
| `src/librarywindow/librarywindowplacement.cpp` | The geometry and screen logic, with no Qt widget |
| `src/test/librarywindowplacementtest.cpp` | 10 tests of that logic |

The hooks into upstream files are small:

- `src/mixxxmainwindow.cpp`: make the manager, give it each new skin, take the
  library back before a skin or Mixxx goes away.
- `src/widget/wsingletoncontainer.cpp`: a container does not take a singleton
  that lives in another window.
- `src/skin/skincontrols.cpp`: the new control.
- `src/widget/wmainmenubar.cpp`: the menu entry, and the application context
  for the full screen shortcut.
- `src/library/librarycontrol.cpp`: make the window of a library widget active
  before the widget takes the focus.

## What is not done

- The QML skin has no library window.
- "Maximize Library" while the library is detached leaves the main window
  almost empty, because that page of the skin holds only small decks and the
  library. Use it only while the library is in the main window.
- The size of the sidebar comes from the splitter of the skin, which keeps one
  ratio for both windows. Drag the splitter in the library window to correct
  it.
- Mixxx writes the geometry of the window when the library comes back and when
  Mixxx stops. A crash loses the last position.
- The shortcut `Ctrl+7` is a default in the code. `res/keyboard/en_US.kbd.cfg`
  has no line for it. To change it, add
  `ViewMenu_ShowLibraryWindow <key>` to the keyboard mapping.
