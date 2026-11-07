# Multidestination Studio 1.1.4

Fixes after the tests of 1.1.3 (all its changes are included). Windows, macOS and Linux.

## Window
- **OBS dialogs open over the studio without the OBS window flashing in front**: Properties, Filters and transition properties are attached to the studio window just before they are shown (they were moved to it after being shown, which made the OBS window appear for a moment).
- **Closing OBS with a Properties or Filters window open no longer crashes**: these windows stay OBS windows, closed by OBS as usual.
- **The OBS main window can be reached again**: clicking it or choosing it with Alt+Tab (Cmd+Tab) brings it in front. The studio still comes back in front when the OBS window is restored after being minimized.

## Selection in the Preview
- **A click always selects the source under the cursor (the highest one)**, even when another source is already selected underneath. Before, a selected source covering the canvas kept the selection and the sources on top of it could not be clicked.
- **Inside a group**: a click on a source selects that source; a click in the group but next to its sources selects the group; **Ctrl+click** (**Cmd+click** on macOS) selects the whole group.
- **Right-click on a source that is part of the selection keeps the selection**: Fit, Stretch, Center and Reset from that menu apply to every selected source.
- **After dragging sources in the Sources list**, only the moved sources stay selected.

## Fit, Stretch, Center
- **A group and one of its sources selected together**: the command applies to the group only.

## Companion
- Same module: **`obs-studio-4.0.0-multidestination.2.tgz`**.

## Installation
- **Windows 10/11**: `multidestination-studio-1.1.4-windows-x64-installer.exe` (not code-signed: SmartScreen may ask for confirmation).
- **macOS**: universal `.pkg` (not signed: System Settings → Privacy & Security → Open Anyway).
- **Ubuntu 24.04**: `multidestination-studio-1.1.4-x86_64-linux-gnu.deb` (`sudo apt install ./multidestination-studio-1.1.4-x86_64-linux-gnu.deb`).

Close OBS before installing. Requires OBS Studio 31.1 or later.
