# Multidestination Studio 1.1.2

Two changes on top of 1.1.1 (all its fixes and additions are included). Windows, macOS and Linux.

## Changed
- **Group sources change in Preview only**: showing another source of a group (Companion Group Source Selector, or the eye in the studio) no longer changes what is on air. The scene on air keeps its own copy of its groups; your choice goes on air with the next transition.
  - Every additional destination: always.
  - Default destination (OBS): when OBS duplicates the scene in Studio Mode (the default setting “Duplicate scene”). If that setting is off, the OBS Program shows the scene itself and its groups change live, as before.
- **Paste shortcuts shown in the right-click menu of a source**: “Paste (reference) (Ctrl+V)” and “Paste (duplicate) (Ctrl+Alt+V)”, and **Cmd+V** / **Cmd+Option+V** on macOS (macOS hides shortcuts in right-click menus).

## Companion
- Same module as 1.1.1: **`obs-studio-4.0.0-multidestination.2.tgz`** (attached below). Nothing to change if you already use it.

## Installation
- **Windows 10/11**: `multidestination-studio-1.1.2-windows-x64-installer.exe` (not code-signed: SmartScreen may ask for confirmation).
- **macOS**: universal `.pkg` (not signed: System Settings → Privacy & Security → Open Anyway).
- **Ubuntu 24.04**: `multidestination-studio-1.1.2-x86_64-linux-gnu.deb` (`sudo apt install ./multidestination-studio-1.1.2-x86_64-linux-gnu.deb`).

Close OBS before installing. Requires OBS Studio 31.1 or later.
