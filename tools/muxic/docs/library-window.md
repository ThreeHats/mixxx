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

If the skin has no library area that can move, the control goes back to 0 and
the log says why.

## The config keys

Mixxx writes these keys to `mixxx.cfg`.

| Key | Content |
|---|---|
| `[Skin] show_library_window` | The state of the toggle, 0 or 1 |
| `[LibraryWindow] geometry` | The result of `QWidget::saveGeometry()`, as base64 |

The window uses `saveGeometry()` and `restoreGeometry()`, the same pair that
the main window uses for `[MainWindow] geometry`. One value holds the frame,
the screen, the scale of the screen and the maximized or full screen state. Qt
moves a window that no screen shows any more onto a screen that is there, and
it refuses a value that comes from a screen of a very different size. On the
first use, and each time Qt refuses the value, the window gets a size of 1000
by 700 in the center of the primary screen.

## What works while the library is detached

- The keyboard shortcuts of the menu bar, because Mixxx gives each one the
  application context. `F11` in the library window makes the main window full
  screen.
- The keyboard mapping of Mixxx (`res/keyboard/*.kbd.cfg`), because the
  window has the same keyboard event filter as the main window.
- The library controls of a controller (`[Library],MoveFocus`, `GoToItem`,
  `MoveVertical` and the others). A control that asks for the focus makes the
  window of the library widget the active window first, but only when Mixxx is
  already the program in front and no modal dialog is open. A controller thus
  cannot pull Mixxx over Ardour or Strudel.
- `[Library],focused_widget`, because Mixxx follows the focus of each window.
- Drag and drop of a track from the track table to a deck of the main window.
- The track context menu, the tooltips and the skin style. The window takes
  the style sheet and the colors of the main window and of the skin.
- A skin reload and a skin change. Mixxx puts the library back in the old skin
  first, then takes the library area of the new skin and opens the window
  again.

## Maximize Library while the library is out

The maximized page of a skin holds the small decks and the place of the
library. While the library is in its own window, that place is empty, thus the
page shows almost nothing. The fork stops the page in that state:

- The control `[Skin],show_maximized_library` stays 0. A controller mapping, a
  skin button or the `Space` key that sets it to 1 gets 0 back immediately.
- The View menu entry "Maximize Library" is grey.
- A library that goes out while the page is maximized gives the usual page
  back first.

The toggle works as before when the library comes back to the main window.

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

A skin that has no library singleton gets no library window. The log says that
the skin has no library area for a window of its own, and the control goes back
to 0. The same happens when a skin fails to load.

## Code

| File | Content |
|---|---|
| `src/librarywindow/librarywindowmanager.cpp` | Finds the library area, moves it, and follows the control |
| `src/librarywindow/wlibrarywindow.cpp` | The window: style, geometry, close |
| `src/librarywindow/librarywindowfocus.cpp` | The rule that says when a window must become active |
| `src/test/librarywindowmanagertest.cpp` | 13 tests of the manager on a skin tree of test widgets |
| `src/test/librarywindowfocustest.cpp` | 5 tests of the focus rule |

The hooks into upstream files are small:

- `src/mixxxmainwindow.cpp`: make the manager, give it each new skin, take the
  library back before a skin or Mixxx goes away.
- `src/widget/wsingletoncontainer.cpp`: a container does not take a singleton
  that lives in another window.
- `src/skin/skincontrols.cpp`: the new control.
- `src/widget/wmainmenubar.cpp`: the menu entry, the application context for
  the full screen shortcut, and the grey "Maximize Library" entry.
- `src/library/librarycontrol.cpp`: make the window of a library widget active
  before the widget takes the focus.
- `res/keyboard/en_US.kbd.cfg`: the line `ViewMenu_ShowLibraryWindow Ctrl+7`,
  next to the other View menu lines.

## What is not done

- The QML skin has no library window.
- The size of the sidebar comes from the splitter of the skin, which keeps one
  ratio for both windows. Drag the splitter in the library window to correct
  it.
- Mixxx writes the geometry of the window when the window closes, when the
  library comes back and when Mixxx stops. A crash loses the last position.
- On Wayland, `activateWindow()` and `raise()` do nothing, because the
  compositor decides which window is in front. A controller that asks for the
  library focus thus moves the focus only inside the active window. Wayland
  also ignores the position of a window, thus only the size comes back.
- The main window in full screen covers the library window when both are on
  one screen. Put the two windows on two screens, or leave full screen.
- To change the shortcut, edit `ViewMenu_ShowLibraryWindow` in the keyboard
  mapping of your language. A mapping with no such line uses `Ctrl+7`.
