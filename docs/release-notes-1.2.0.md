# Multidestination Studio 1.2.0

Everything new since **1.1.6**. Windows, macOS and Linux · OBS Studio 31.1 or later.

## New

### Show menu
A **show** is an OBS scene collection with all its Multidestination Studio settings (destinations, scenes, cues, quick transitions, output blending). The new **Show** menu creates, saves, saves as, loads, imports and exports shows. When the show has changed, Multidestination Studio asks whether to save it when you close the studio, quit OBS, or load another show.

### Frames
**+** in the Sources dock → **Frame…** adds an empty frame to a destination, shown as a dotted outline in the studio only. Drop files from your computer onto it: they fill the frame with their proportions kept. A frame moves and resizes like a group, and the Companion Group Source Selector cycles through its contents.

### macOS output screens
Output windows stay on their screen on every Space and cover the whole screen, menu bar and Dock included, without needing “Displays have separate Spaces”.

## Improvements
- Updated texts for the Cues setting and the Output blending hint.
- The studio title shows the loaded show. Everything new is translated in English, French and Norwegian.

## Companion
The Companion module is unchanged: `obs-studio-4.0.0-multidestination.3.tgz` (attached). If you already installed it with 1.1.6, there is nothing to do.

## Installation
- **Windows 10/11**: `multidestination-studio-1.2.0-windows-x64-installer.exe` (not code-signed: SmartScreen may ask for confirmation).
- **macOS**: universal `.pkg` (not signed: System Settings → Privacy & Security → Open Anyway).
- **Ubuntu 24.04**: `multidestination-studio-1.2.0-x86_64-linux-gnu.deb` (`sudo apt install ./multidestination-studio-1.2.0-x86_64-linux-gnu.deb`).
- **User guide (PDF)**: [Multidestination-Studio-1.2.0-User-Guide.pdf](https://github.com/sebts-574/multidestination-studio/blob/main/docs/Multidestination-Studio-1.2.0-User-Guide.pdf).

Close OBS before installing. Requires OBS Studio 31.1 or later.
