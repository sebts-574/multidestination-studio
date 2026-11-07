# Multidestination Studio 1.1.5

Everything new since **1.1.0**. Windows, macOS and Linux · OBS Studio 31.1 or later.

> ## 🎛️ New Companion module: `obs-studio-4.0.0-multidestination.2.tgz`
>
> This release comes with a **new version of the Companion module** (attached below). It adds a **group source selector**, a **running cue status** button and new variables, and follows the new cue behaviour.
>
> **To update, on every computer running Companion:**
> 1. Download `obs-studio-4.0.0-multidestination.2.tgz` below (do not unzip it).
> 2. In Companion: **Modules → Import module package**, choose the file.
> 3. Open your OBS connection and set its **module version** to **4.0.0-multidestination.2**. Companion does not switch it by itself: until you do, the connection keeps the previous version.
>
> Full guide: [Companion / Stream Deck](https://github.com/sebts-574/multidestination-studio/blob/main/docs/companion-module.md).

## New

### Cues
- **A cue in Preview**: click a cue to put it in Preview (the Preview title shows its name). **Transition** or a **quick transition** then runs it.
- **The first scene follows the launch**: it has no transition of its own and uses the transition that starts the cue: the Transition button (default transition), a quick transition (its transition and duration), or the default transition for the Play button of the Cues dock.
- **Stop cue fades to black** on every unlocked destination; the Preview is not changed.
- **Cues are independent of the Preview**: you can keep working on your scenes while a cue plays. Putting a scene on air yourself (Transition, quick transition, T-bar, OBS, Companion) stops the cue.
- **End of a cue**: the last scene stays on air and the Preview is unchanged. The next take is up to the operator, or put a transition to a background in the cue itself.
- **Auto-follow** (formerly “Next at media end”): the next scene starts as soon as the media of the scene ends, in any destination. The media position is shown while it plays.
- **Delete / Backspace** deletes the selected cue.

### Editing in the Preview
- **Click selection**: a click selects the highest source under the cursor, also inside a group. A click in a group next to its sources selects the group. **Ctrl+click** (**Cmd+click** on macOS) selects the whole group.
- **Several sources at once**: select them in the Sources dock, then drag one of them to move them together, drag a handle of their common frame to resize them together, or use the arrow keys. When a group and one of its sources are both selected, the group wins.
- **Fit to screen keeps the proportions** of the source. Fit, Stretch, Center and Reset work for sources inside a group and apply to every selected source (also from the right-click menu in the Preview).
- **Crop…** in the right-click menu of a source: a preview of the whole source with the cropped areas darkened, draggable edges, corners and frame, and the values in pixels. Ctrl+Z (Cmd+Z) undoes in the dialog.
- **Copy / paste** keep the place on screen, also for sources inside a group. The right-click menu shows the paste shortcuts: Ctrl+V / Ctrl+Alt+V, Cmd+V / Cmd+Option+V on macOS (Ctrl+Shift+V stays Paste Transform, as in OBS).

### Sources, groups and scenes
- **Move to group / Remove from group** in the right-click menu of a source; dragging into or out of a group also keeps the place on screen.
- **Group sources change in Preview only**: showing another source of a group (studio or Companion) does not change what is on air; it goes on air with the next transition. On the default destination, this needs OBS's Studio Mode setting “Duplicate scene” (on by default).
- **Duplicate…** in the right-click menu of a scene: name of the copy and destinations. The copy has the same sources, placed the same way.

### Window
- **OBS dialogs open over the studio**: Properties, Filters and transition properties no longer bring the OBS window in front of Multidestination Studio.
- **The studio comes back in front** when the OBS window is restored after being minimized. The OBS window stays reachable with a click or Alt+Tab.

### Companion (module 4.0.0-multidestination.2)
- **Group Source Selector**: one button per source group (groups with the same name share one button), showing the group name and the source shown; each press shows the next source and hides the others. One button per source of each group, lit while it is shown.
- **Running Cue Status** button: running cue, step and time left (wait or Auto-follow media).
- **Stop Cue** fades to black. A cue in Preview runs with **Transition** or **Transition With**, using that transition for its first scene.
- **New variables**: `mds_preview_scene`, `mds_program_scene`, `mds_preview_cue`, `mds_running_cue`, `mds_cue_step`, `mds_cue_remaining`, `mds_cue_media`, `mds_cue_status`, `mds_group_<group>`.
- **New vendor requests** (vendor `multidestination-studio`): `CycleGroup`, `SelectGroupSource`; `GetState` also returns the groups and the progress of the running cue; `GoCue` accepts a transition.

## Bug fixes
- Sources no longer move when a group is created, when they enter or leave a group, or when they are pasted.
- Editing a source inside a group no longer moves the whole group; Fit to screen works inside groups.
- Group names no longer show as “unnamed” when you modify the Program; live edits no longer copy OBS's private copies into the scene.
- Auto-follow (Next at media end) now starts the next scene when the media ends.
- Ctrl+Z (Cmd+Z) works again in the studio, including for sources inside a group.
- No more unwanted multi-selection in the Scenes, Sources and Cues docks (macOS).
- Delete in the Cues dock no longer asks to remove a source (macOS).

## Installation
- **Windows 10/11**: `multidestination-studio-1.1.5-windows-x64-installer.exe` (not code-signed: SmartScreen may ask for confirmation).
- **macOS**: universal `.pkg` (not signed: System Settings → Privacy & Security → Open Anyway).
- **Ubuntu 24.04**: `multidestination-studio-1.1.5-x86_64-linux-gnu.deb` (`sudo apt install ./multidestination-studio-1.1.5-x86_64-linux-gnu.deb`).
- **Companion**: `obs-studio-4.0.0-multidestination.2.tgz`, see above.
- **User guide (PDF)**: [Multidestination-Studio-1.1.5-User-Guide.pdf](https://github.com/sebts-574/multidestination-studio/blob/main/docs/Multidestination-Studio-1.1.5-User-Guide.pdf).

Close OBS before installing. Requires OBS Studio 31.1 or later.
