# Multidestination Studio 1.1.8

## New: Show menu
A **show** is an OBS scene collection with all the Multidestination Studio settings it holds (destinations, scenes, cues, quick transitions, output blending). The new **Show** menu, between Docks and Configuration Settings:
- **New show…**: creates a new scene collection and loads it.
- **Save show** / **Save show as…**: saves the current state in the loaded show, or as a new show (the previous one stays as it was last saved).
- **Load show**: lists the shows, the loaded one is checked.
- **Import…** / **Export…**: the OBS scene collection importer and exporter. After importing one show, you can load it right away.
- **Save prompt**: when the show has changed, you are asked to save it (Save / Don't save / Cancel) when closing the studio, when quitting OBS, and before New show or Load show. Preview and Program changes do not count.
- The studio title shows the loaded show. Translated in English, French and Norwegian.

Good to know: OBS keeps saving the scene collection by itself. **Don't save** puts the show file back as it was last saved (at OBS exit, or before loading another show); when you close only the studio, the changes stay in OBS and you are asked again when OBS quits. Switching collection from the OBS menu keeps the OBS behaviour (saved without asking).

## Companion
- Module: `obs-studio-4.0.0-multidestination.3.tgz` (unchanged).

## Installation
- **Windows 10/11**: `multidestination-studio-1.1.8-windows-x64-installer.exe` (not code-signed: SmartScreen may ask for confirmation).
- **macOS**: universal `.pkg` (not signed: System Settings → Privacy & Security → Open Anyway).
- **Ubuntu 24.04**: `multidestination-studio-1.1.8-x86_64-linux-gnu.deb` (`sudo apt install ./multidestination-studio-1.1.8-x86_64-linux-gnu.deb`).
- **User guide (PDF)**: [Multidestination-Studio-1.2.1-User-Guide.pdf](https://github.com/sebts-574/multidestination-studio/blob/main/docs/Multidestination-Studio-1.2.1-User-Guide.pdf) (latest guide).

Close OBS before installing. Requires OBS Studio 31.1 or later.
