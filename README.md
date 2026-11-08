# Multidestination Studio

**Multidestination Studio** is a plugin for [OBS Studio](https://obsproject.com) that turns OBS into a multi-output show controller. It adds independent **destinations** (an LED wall, a set of blended projectors, stage monitors, the stream), each with its own resolution, Preview and Program, all driven from one full-screen studio view with a single TAKE.

Version **1.1.7** · Windows, macOS, Linux · OBS Studio 31.1 or later · GPL-2.0-or-later

## Features

- **Destinations**: the OBS main canvas plus as many extra destinations as you need, each with its own canvas, resolution and physical output screen.
- **Shared scenes**: one scene list for every destination. Each scene chooses which destinations it covers, and each destination keeps its own version of the scene (its own sources and layout).
- **Multidestination Studio view**: a full-screen workspace in the OBS style, with stacked Preview and Program columns and docks for scenes, sources, media controls, audio mixer, scene transitions and controls.
- **One TAKE for every destination**: Transition, quick transitions and the T-bar use the OBS transitions. Locked destinations, and destinations the scene does not cover, keep their Program.
- **Protected live output**: what is on air is a copy of the scene. "Modify PGM" lets you edit the live picture directly, and those edits are kept in the scene.
- **Mouse editing**: move, resize and crop (also in a Crop dialog with a preview), centre snapping, undo, reordering, several sources moved or resized together, sources edited inside their group, drag and drop of media files.
- **Organisation**: scene folders (collapsible), OBS source groups, scene duplication.
- **Cues** (optional): play several scenes one after the other, each with its pre-wait, transition, duration and post-wait, or Auto-follow at the end of a video; Stop fades to black.
- **Companion / Stream Deck**: a Companion module with Multidestination Studio presets (scenes, transitions, settings, cues, cue status, group source selector), and obs-websocket vendor requests (`multidestination-studio`).
- **Transitions**: every OBS transition type can be added from the studio, plus quick transitions between Preview and Program.
- **Output blending**: spread one destination across several projectors with automatic soft-edge blending, gamma, curve, black level, corner pin, a per-output aspect ratio lock and a built-in test pattern.
- **Projection**: open any destination fullscreen on its screen, or one window per projector for a blended destination.
- **Languages**: English, French and Norwegian (Bokmål), selectable independently from the OBS language.

## Installation

Download the file for your system from the [Releases](https://github.com/sebts-574/multidestination-studio/releases) page and close OBS before installing.

| System | File | Notes |
|---|---|---|
| Windows 10/11 (x64) | `multidestination-studio-1.1.7-windows-x64-installer.exe` | Installs to `C:\ProgramData\obs-studio\plugins\multidestination-studio`. The installer is not code-signed: Windows SmartScreen may ask you to confirm. |
| macOS (Apple Silicon and Intel) | the `.pkg` package (universal) | Not signed: System Settings → Privacy & Security → Open Anyway. |
| Ubuntu 24.04 (x86_64) | `multidestination-studio-1.1.7-x86_64-linux-gnu.deb` | `sudo apt install ./multidestination-studio-1.1.7-x86_64-linux-gnu.deb` |
| Bitfocus Companion | `obs-studio-4.0.0-multidestination.3.tgz` | Companion OBS Studio module with the Multidestination Studio presets: see [Companion / Stream Deck](docs/companion-module.md). |

Then open OBS and click **Multidestination Studio** in the Controls dock (or Tools → Multidestination Studio).

If you used a test version named `destinations-for-obs`, the Windows installer removes it. On macOS and Linux, remove it manually so OBS does not load both.

## Quick start

1. Open Multidestination Studio. The default destination is your OBS canvas.
2. **Destinations → New destination…**: give it a name, a resolution and an output screen.
3. Add a scene (Scenes dock **+**) and tick the destinations it should appear on.
4. Add sources in each destination's Preview, place them with the mouse.
5. Press **Transition** or **Cut**. Open the destination on its screen from the **Destinations** menu.

The full user guide is in [`docs/`](docs/).

To control the studio from Bitfocus Companion or a Stream Deck, see [Companion / Stream Deck](docs/companion-module.md).

## Building from source

The project uses the [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate) build system (CMake presets, Qt 6, OBS 31.1.7 dependencies downloaded automatically).

```sh
cmake --preset windows-x64      # or macos, ubuntu-x86_64
cmake --build --preset windows-x64
```

Every push to `main` builds Windows, macOS and Linux packages in GitHub Actions. Pushing a tag such as `1.0.3` creates a draft release with all packages attached.

## Project layout

```
src/            plugin source (C++17, Qt 6)
data/locale/    en-US, fr-FR, nb-NO translations
data/effects/   blend.effect (output blending shader)
build-aux/      Windows installer script (Inno Setup)
docs/           release notes and user guide
```

## License

Copyright © 2026 Sébastien Tombre. Released under the GNU General Public License v2.0 or later (see [LICENSE](LICENSE)), as required for OBS Studio plugins.
