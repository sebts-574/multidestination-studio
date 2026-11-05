# Multidestination Studio 1.1.0

Everything new since 1.0.7.

## New

### Control from Companion / Stream Deck
- **Companion module included**: `obs-studio-4.0.0-multidestination.1.tgz` (attached below) is the Companion **OBS Studio** module with a **Multidestination Studio** preset section. Import it in Companion (**Modules → Import module package**) and select version **4.0.0-multidestination.1** for your OBS connection. Step-by-step guide: [Companion / Stream Deck](https://github.com/sebts-574/multidestination-studio/blob/main/docs/companion-module.md).
- **Presets**: Scene to Preview and Scene to Program (one per scene, green in Preview, red in Program), Transition, Transition With (one per OBS transition), After a Transition and Destinations Shown toggles, Cue to Preview and Stop Cue. The buttons follow the studio live.
- **obs-websocket vendor requests** (vendor `multidestination-studio`), also usable from any obs-websocket client: `GetState`, `SetPreviewScene`, `SetProgramScene`, `Transition`, `Cut`, `SetAfterTransition`, `SetDestinationsShown`, `SetPreviewCue`, `GoCue`, `StopCue`. Events: `StateChanged`, `PreviewSceneChanged`, `ProgramSceneChanged`.

### Cues (optional, off by default)
- **Configuration Settings → Cues… → Activate Cues** shows the **Cues** dock, between Sources and Media controls.
- A cue plays several scenes one after the other. For each scene: **pre-wait**, **transition** (Default or any OBS transition), **duration**, and either a **post-wait** or **Next at media end** (the next scene starts when the video or audio of the scene ends).
- The duration follows the transition, as in the OBS dock: Default → default duration; Cut → greyed; other transitions → editable (300 ms proposed).
- Click a cue to show its first scene in Preview, then press **Transition** to run it. **Run** and **Stop** buttons in the dock.
- **Live status** on the running scene: pre-wait and post-wait countdowns, or media time (0:12 / 0:45).
- New cues start from the scenes selected in the Scenes dock. Cues are saved with the scene collection.

### Transitions, as in OBS Studio Mode
- **Quick transitions** under the Transition button: **+** creates one from a transition type (Cut, Fade, Swipe, Slide, Stinger, Fade to Color, Luma Wipe, Move) with the duration chosen at the bottom of the menu. The arrow of each quick transition edits it (Properties…, duration) or removes it. Quick transitions are saved with the scene collection.
- **T-bar**: drag to the end to finish the transition on every destination, back to the start to cancel it. Cut and Stinger are replaced by a fade, as in OBS.
- **⋮ menu** next to Transition: “Previous Program returns to Preview” and “Show all destinations”.
- **Remove transition** button in the Scene Transitions dock.

### Docks in the OBS style
- Toolbars with the OBS theme icons, rounded rows, source type icons, OBS eye and lock icons. A locked source cannot be moved with the mouse.
- The open/close arrow of folders, groups and cues is inside the row; items outside folders are not shifted.
- **Folder** button in the Sources dock: creates a new folder (OBS group), with the selected sources in it, ready to rename.
- **Scene filters** button in the Scenes dock.

## Changed
- **Multidestination Studio is an independent window**, like an OBS projector: it has its own entry in the taskbar (Dock and Mission Control on macOS). After minimizing it, you get back to it from there, without the OBS button.
- **The Cut button between Preview and Program is gone**: add Cut as a quick transition if you need it. The Cut hotkey is unchanged.
- **Configuration Settings menu**: After a transition, Destinations shown, Cues…, Startup Settings, Language.

## Installation
- **Windows 10/11**: `multidestination-studio-1.1.0-windows-x64-installer.exe` (not code-signed: SmartScreen may ask for confirmation).
- **macOS**: universal `.pkg` (not signed: System Settings → Privacy & Security → Open Anyway).
- **Ubuntu 24.04**: `multidestination-studio-1.1.0-x86_64-linux-gnu.deb` (`sudo apt install ./multidestination-studio-1.1.0-x86_64-linux-gnu.deb`).
- **Companion**: `obs-studio-4.0.0-multidestination.1.tgz`, see the guide above.

Close OBS before installing. Requires OBS Studio 31.1 or later.
