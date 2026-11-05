# Control Multidestination Studio from Companion / Stream Deck

Multidestination Studio adds its own buttons to the **OBS Studio** module of Bitfocus Companion: scenes to Preview or Program on every destination, transitions, the two studio settings and cues. They come with a build of the OBS Studio module that you install once in Companion.

## What you need

- **OBS Studio 31.1 or later** with the **Multidestination Studio** plugin installed.
- **Bitfocus Companion**, recent enough to offer version **4.0.0** of the OBS Studio module in its module list. If it does not, update Companion first.
- The computer running Companion must reach the computer running OBS on the network (or be the same computer).

## 1. Turn on the OBS WebSocket server

1. In OBS: **Tools → WebSocket Server Settings**.
2. Tick **Enable WebSocket server**.
3. Note the **Server Port** (4455 by default) and, under **Show Connect Info**, the **Server IP** and **Server Password**.
4. Click **OK**.

## 2. Download the module

1. Open the repository's release **[Companion module for Multidestination Studio](https://github.com/sebts-574/multidestination-studio/releases/tag/companion-module)**.
2. Under **Assets**, download `companion-obs-studio-multidestination-<version>.tgz`.
3. Keep the file as it is: **do not unzip it**. Companion only imports `.tgz` files.

## 3. Import it in Companion

1. In Companion, open **Modules**.
2. Click **Import module package** and choose the `.tgz` file.
3. The module does not appear as a new module: it is added as one more **version of the OBS Studio module** (for example **4.0.1**, next to the official 4.0.0). This is expected.

## 4. Use this version for your OBS connection

1. Open **Connections**.
2. **New OBS connection**: add **OBS Studio**, choose the Multidestination Studio version (4.0.1) when Companion asks for the version, then enter the IP address, port and password from step 1.
3. **Existing OBS connection**: open its settings and change the **module version** to the Multidestination Studio version (4.0.1). Your existing buttons keep working.
4. The connection turns green when it reaches OBS.

## 5. Add the buttons

1. Open **Buttons**, then the **Presets** tab, then **OBS**.
2. Open the **Multidestination Studio** section. It appears only while OBS is running with the plugin and the connection is green.
3. Drag the buttons you want onto your pages:

| Preset | What it does | Feedback |
| --- | --- | --- |
| **Scene to Preview** (one per scene) | Shows the scene in the studio Preview | Green in Preview, red in Program |
| **Scene to Program** (one per scene) | Puts the scene on air on every unlocked destination it covers | Green in Preview, red in Program |
| **Transition** | Takes the studio Preview to Program with the current transition | — |
| **Transition With** (one per OBS transition) | Takes the Preview with that transition | — |
| **After a Transition** | Toggles “previous Program returns to Preview” | Lit when on |
| **Destinations Shown** | Toggles “show all destinations” / “only the destinations of the selected scene” | Lit when on |
| **Cue to Preview** (one per cue) | Shows the first scene of the cue in Preview; Transition then runs the cue | Green in Preview, red while running |
| **Stop Cue** | Stops the running cue | — |

The cue presets appear only when **Configuration Settings → Cues… → Activate Cues** is on in Multidestination Studio.

The buttons follow the studio live: changing a scene, a setting or a cue in Multidestination Studio updates their colours. New scenes, transitions and cues appear in the presets automatically.

The same features are available as actions and feedbacks named **Multidestination Studio - …**, to build your own buttons.

All other OBS features of the module (recording, streaming, sources, audio…) work exactly as in the official version.

## Updating

- **New Multidestination Studio module**: download the new `.tgz` from the same release and import it (step 3), then select the new version in your connection (step 4).
- **Official OBS Studio module updates**: Companion may offer newer official versions of the OBS Studio module. Switching your connection to an official version removes the Multidestination Studio buttons (their actions become unknown). Stay on the Multidestination Studio version, and wait for a Multidestination Studio build based on the new official version.
- **Old builds**: in **Modules → OBS Studio**, you can delete Multidestination Studio versions you no longer use (bin icon), once no connection uses them.

## Troubleshooting

- **No “Multidestination Studio” section in the presets**
  - Check that the connection uses the Multidestination Studio version, not the official one.
  - Check that the connection is green and that OBS runs with the plugin (Multidestination Studio 1.0.9 or later).
  - Restart OBS, then disable and re-enable the connection in Companion.
- **Import refused**: the file must be the `.tgz` itself, not a `.zip`. If Companion still refuses it, update Companion.
- **Scene to Preview does nothing**: the scene name must exist in OBS; after renaming a scene, use the new preset (the presets list updates automatically).

## How it works (for the curious)

The build is the official OBS Studio module with additions. When the connection starts, the module asks OBS whether the Multidestination Studio plugin is there (an obs-websocket vendor request to `multidestination-studio`). If the plugin answers, the module shows the section and builds one button per scene, transition and cue. Each button sends a request to the plugin (`SetPreviewScene`, `SetProgramScene`, `Transition`, `SetAfterTransition`, `SetDestinationsShown`, `SetPreviewCue`, `GoCue`, `StopCue`). The plugin sends a `StateChanged` event whenever something changes, and the module reads the new state to update the button colours. Without the plugin, the module behaves exactly like the official one.
