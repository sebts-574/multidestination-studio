# Multidestination Studio 1.1.1

Fixes and additions from real-world use (tested at Jarlsberg on macOS). The fixes apply to Windows, macOS and Linux.

## Cues
- **Stop cue fades to black**: the Stop button of the Cues dock (and the Companion Stop Cue button) stops the cue with a fade to black on every unlocked destination. The Preview is not changed.
- **A cue in Preview**: clicking a cue puts it in Preview (the Preview title shows the cue). **Transition** or a **quick transition** then runs the cue.
- **The first scene of a cue has no transition of its own**: it uses the transition that starts the cue: the Transition button (default transition), a quick transition (its transition and duration), or the default transition for the Play button of the dock.
- **Cues are independent of the Preview**: you can keep working and change the Preview while a cue plays; the cue goes on. Putting a scene on air yourself (Transition, quick transition, T-bar, scene changed in OBS or from Companion) stops the cue. At the end of a cue, the previous Program returns to Preview only if you did not change the Preview in the meantime.
- **Auto-follow** (formerly “Next at media end”) works: the media of the scene is searched in every destination, its end is signalled by OBS (with a check every 250 ms as a backup), and the cue shows the media position while it plays.
- **Delete / Backspace** deletes the selected cue (on macOS, the key was taken by the menu and asked to remove a source).

## Sources and groups
- **Groups keep the placement**: creating a group, dragging a source into a group or out of it no longer moves the sources on screen, and no longer changes their stacking order.
- **New: Move to group / Remove from group** in the right-click menu of a source.
- **Editing one source of a group**: select it in the list, then move or resize it in the Preview with the handles; only that source changes, not the whole group.
- **Copy / paste**: a source copied from a group is pasted at the same place on screen. Paste Transform also keeps the place on screen between a group and the scene.
- **Group names stay correct when you modify the Program** (they could show as “unnamed”).
- **New: Crop…** in the right-click menu of a source (and Edit → Transform): left, right, top, bottom in pixels, applied live; Cancel restores the previous crop.

## Scenes
- **New: Duplicate…** in the right-click menu of a scene: name of the copy and destinations. The copy has the same sources, placed the same way, in each checked destination that shows the original.

## Selection
- **No more unwanted multi-selection** in the Scenes, Sources and Cues docks: the Ctrl / Cmd and Shift keys are read from the keyboard at the moment of the click (a key seen as still pressed after a shortcut or an app switch could add to the selection).

## Keyboard
- **Ctrl+Shift+V (Cmd+Shift+V on macOS) is not used for “Paste (duplicate)”**: OBS already uses it for **Paste Transform**, and so does the studio. Paste (duplicate) stays on **Ctrl+Alt+V** (**Cmd+Option+V** on macOS).

## Companion
- New module version **`obs-studio-4.0.0-multidestination.2.tgz`** (attached below). Import it, then set your OBS connection to version **4.0.0-multidestination.2** (Modules → OBS Studio: the plug icon must be on that version), on every computer running Companion.
- **Group Source Selector**: one button per source group (groups with the same name share one button). It shows the group name and the source shown; each press shows the next source and hides the others. One button per source of each group is also provided, lit while that source is shown.
- **Stop Cue** fades to black. **Running Cue Status** button: step and time left (wait or Auto-follow media).
- A cue in Preview runs with **Transition** or a **Transition With** button, using that transition for its first scene.
- New variables: `mds_preview_scene`, `mds_program_scene`, `mds_preview_cue`, `mds_running_cue`, `mds_cue_step`, `mds_cue_remaining`, `mds_cue_media`, `mds_cue_status`, `mds_group_<group>`.
- New vendor requests: `CycleGroup` and `SelectGroupSource`; `GetState` also returns the groups and the progress of the running cue; `GoCue` accepts a transition.

## Installation
- **Windows 10/11**: `multidestination-studio-1.1.1-windows-x64-installer.exe` (not code-signed: SmartScreen may ask for confirmation).
- **macOS**: universal `.pkg` (not signed: System Settings → Privacy & Security → Open Anyway).
- **Ubuntu 24.04**: `multidestination-studio-1.1.1-x86_64-linux-gnu.deb` (`sudo apt install ./multidestination-studio-1.1.1-x86_64-linux-gnu.deb`).
- **Companion**: `obs-studio-4.0.0-multidestination.2.tgz`, see [Companion / Stream Deck](https://github.com/sebts-574/multidestination-studio/blob/main/docs/companion-module.md).

Close OBS before installing. Requires OBS Studio 31.1 or later.
