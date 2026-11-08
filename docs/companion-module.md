# Companion / Stream Deck: install the Multidestination Studio module

This guide explains, step by step, how to control Multidestination Studio from **Bitfocus Companion** (and therefore from a Stream Deck or any surface Companion supports).

Multidestination Studio comes with a version of Companion's **OBS Studio** module that adds a **Multidestination Studio** preset section: scenes to Preview or Program on every destination, transitions, the studio settings and cues. You install this module once in Companion, then use it for your OBS connection.

---

## What you need

- **OBS Studio 31.1 or later**, with **Multidestination Studio 1.1.0 or later** installed.
- **Bitfocus Companion**, up to date. The module is based on version 4.0.0 of the OBS Studio module: your Companion must be recent enough to run it (if Companion refuses the file, update Companion first).
- Companion and OBS on the same computer, or on two computers that can reach each other on the network.

---

## Step 1 — Download the module file

1. Open the [Multidestination Studio releases](https://github.com/sebts-574/multidestination-studio/releases) and choose the release you installed in OBS.
2. Under **Assets**, download **`obs-studio-4.0.0-multidestination.3.tgz`**.
3. **Do not unzip or rename the file.** Companion only imports the `.tgz` file itself.

---

## Step 2 — Turn on the OBS WebSocket server

Companion talks to OBS through the WebSocket server built into OBS.

1. In OBS, open **Tools → WebSocket Server Settings**.
2. Tick **Enable WebSocket server**.
3. Note the **Server Port** (4455 by default).
4. Click **Show Connect Info** and note the **Server IP** and the **Server Password**.
5. Click **OK**.

---

## Step 3 — Import the module in Companion

1. Open the Companion web interface (the **Launch GUI** button of Companion).
2. Go to **Modules**.
3. Click **Import module package**.
4. Choose `obs-studio-4.0.0-multidestination.3.tgz`.

The module does **not** appear as a new, separate module. It is added as one more **version of the OBS Studio module**, named **4.0.0-multidestination.3**, next to the official versions (4.0.0, 3.15.x…). This is expected: it is the full OBS Studio module, plus the Multidestination Studio additions.

> **Important:** importing the module does **not** change the version used by your connections. A connection you created before keeps using the official version until you change it (step 4). This is the most common reason why the Multidestination Studio buttons do not appear.

---

## Step 4 — Use this version for your OBS connection

**If you do not have an OBS connection yet**

1. Go to **Connections** and click **Add connection**.
2. Search for **OBS Studio** and add it.
3. When Companion asks for the version, choose **4.0.0-multidestination.3**.
4. Enter the **IP address**, **port** and **password** from step 2, then save.

**If you already have an OBS connection** (this step is required)

1. Go to **Connections** and open your OBS Studio connection.
2. Change its **module version** from the official version (for example 4.0.0 or 3.15.x) to **4.0.0-multidestination.3**, then save.
3. Your existing buttons keep working.

You can check which version a connection uses in **Modules → OBS Studio**: the plug icon marks the version in use. If it is not on 4.0.0-multidestination.3, go back to step 4.

Do this on **every computer** running Companion: the version is chosen separately on each Companion installation.

The connection turns **green** when Companion reaches OBS.

---

## Step 5 — Add the Multidestination Studio buttons

1. Go to **Buttons** and open the **Presets** tab.
2. Open **OBS**, then the **Multidestination Studio** section.
   The section is only shown while OBS is running with the plugin and the connection is green.
3. Drag the buttons you want onto your pages.

| Preset | What it does | Button colour |
| --- | --- | --- |
| **Scene to Preview** (one per scene) | Shows the scene in the studio Preview | Green in Preview, red in Program |
| **Scene to Program** (one per scene) | Puts the scene on air on every unlocked destination it covers | Green in Preview, red in Program |
| **Transition** | Takes the studio Preview to Program with the current transition | — |
| **Transition With** (one per OBS transition) | Takes the Preview to Program with that transition | — |
| **After a Transition** | Toggles “the previous Program returns to Preview” | Lit when on |
| **Destinations Shown** | Toggles “all destinations” / “only the destinations of the selected scene” | Lit when on |
| **Cue to Preview** (one per cue) | Puts the cue in Preview; **Transition** or a **Transition With** button then runs it (its first scene uses that transition) | Green in Preview, red while running |
| **Stop Cue** | Stops the running cue with a fade to black | — |
| **Running Cue Status** | Shows the running cue, its step and the time left (wait, or Auto-follow media); “No cue running” when idle | Red while a cue runs |
| **Group** (one per source group) | Shows the group name and the source shown; each press shows the next source of the group and hides the others | — |
| **Group: show …** (one per source of each group) | Shows that source of the group and hides the others | Lit while shown |

The cue presets only appear when **Configuration Settings → Cues… → Activate Cues** is on in Multidestination Studio.

The group buttons only change the **Preview**: the scene on air keeps its sources until the next transition, which takes your choice on air.

Groups with the same name are switched together: the same group used in several scenes, or the same group recreated in another destination, has one button. Their sources are matched by name.

Variables (for your own buttons): `$(obs:mds_preview_scene)`, `$(obs:mds_program_scene)`, `$(obs:mds_preview_cue)`, `$(obs:mds_running_cue)`, `$(obs:mds_cue_step)`, `$(obs:mds_cue_remaining)`, `$(obs:mds_cue_media)`, `$(obs:mds_cue_status)`, `$(obs:mds_cue_button)`, and `$(obs:mds_group_<group name>)` (spaces and special characters become `_`). Replace `obs` with your connection label if you renamed it.

The buttons follow the studio live: when you change a scene, a setting or a cue in Multidestination Studio, their colours update. New scenes, transitions and cues appear in the presets automatically.

To build your own buttons, the same features are available as **actions** and **feedbacks** named **Multidestination Studio - …** in the OBS connection.

All the other OBS features of the module (recording, streaming, sources, audio, media…) work exactly as in the official module.

---

## Updating

- **New Multidestination Studio module**: when a release brings a new `.tgz`, import it (step 3) and select the new version in your connection (step 4). You can then delete the old version in **Modules → OBS Studio** (bin icon).
- **Official OBS Studio module updates**: Companion may offer newer official versions. If you switch your connection to an official version, the Multidestination Studio buttons stop working (their actions are unknown to it). Keep the Multidestination Studio version.

---

## Troubleshooting

**Companion refuses the file**
- Import the `.tgz` file itself, not a `.zip` and not an unpacked folder.
- Update Companion, then try again.

**No “Multidestination Studio” section in the presets**
- Most often, the OBS connection still uses the official version. Open the connection and set its module version to **4.0.0-multidestination.3** (step 4). In **Modules → OBS Studio**, the plug icon must be on that version.
- Check that the connection is green and that OBS runs with Multidestination Studio 1.1.0 or later.
- Restart OBS, then disable and re-enable the connection in Companion.

**A button does nothing**
- The scene, transition or cue may have been renamed or removed. Drag the preset again: the list is always up to date.

---

## How it works

When the connection starts, the module asks OBS whether the Multidestination Studio plugin is present (an obs-websocket vendor request to `multidestination-studio`). If the plugin answers, the module shows the section and builds one button per scene, transition and cue. Each button sends a request to the plugin (`SetPreviewScene`, `SetProgramScene`, `Transition`, `SetAfterTransition`, `SetDestinationsShown`, `SetPreviewCue`, `GoCue`, `StopCue`). The plugin sends a `StateChanged` event whenever something changes, and the module reads the new state to update the colours. Without the plugin, the module behaves exactly like the official one.
