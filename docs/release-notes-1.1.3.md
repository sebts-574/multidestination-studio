# Multidestination Studio 1.1.3

Fixes and additions after the first tests of 1.1.1 / 1.1.2 (all their changes are included). Windows, macOS and Linux.

## Window
- **OBS dialogs stay in front of the studio**: Properties (double-click or gear), Filters, transition properties and the properties of a new source open over Multidestination Studio, instead of bringing the OBS window in front of it.
- **Multidestination Studio comes back in front**: when you come back to OBS (taskbar, Dock, or the OBS window restored after being minimized), the open studio window is shown in front of the OBS window.

## Editing
- **Ctrl+Z (Cmd+Z) works again** in the studio (OBS's own Undo shortcut was blocking it). Moving or resizing a source inside a group is undone too.
- **Selection in the Preview**: a click selects the source under the cursor, alone, also inside a group. A click inside a group but next to its sources selects the group. **Ctrl+click** (**Cmd+click** on macOS) selects the whole group.
- **Fit to screen keeps the proportions of the source**, works for sources inside a group, and applies to every selected source (also Stretch, Center and Reset transform).
- **Crop… dialog with a preview**: the whole source is shown with the cropped areas darkened; drag the edges or corners of the frame to crop, or drag inside it to move it. The values follow.

## Cues
- **End of a cue**: the last scene stays on air and the Preview is not changed. The next take is up to the operator, or put the transition to a background in the cue itself.

## Companion
- Same module: **`obs-studio-4.0.0-multidestination.2.tgz`**.

## Installation
- **Windows 10/11**: `multidestination-studio-1.1.3-windows-x64-installer.exe` (not code-signed: SmartScreen may ask for confirmation).
- **macOS**: universal `.pkg` (not signed: System Settings → Privacy & Security → Open Anyway).
- **Ubuntu 24.04**: `multidestination-studio-1.1.3-x86_64-linux-gnu.deb` (`sudo apt install ./multidestination-studio-1.1.3-x86_64-linux-gnu.deb`).

Close OBS before installing. Requires OBS Studio 31.1 or later.
