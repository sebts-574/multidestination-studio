# Multidestination Studio 1.1.6

Output blending: aspect ratio lock and a new layout of the output settings. Windows, macOS and Linux.

## Output blending
- **Aspect ratio lock** for each output: the lock between width and height keeps the proportions when the output is resized, with the mouse or by typing a size. On by default (as before); turn it off to resize width and height freely. Shift does the opposite while dragging.
- **New layout of the output settings**: size with the lock between width and height, position, and the bottom-right corner (read-only); the edge blend widths laid out like the edges of the output (top, left and right, bottom) with Automatic blend widths below; then gamma, curve and black level. Corner pin is unchanged. No setting was removed.

## Companion
- Same module: **`obs-studio-4.0.0-multidestination.2.tgz`**.

## Installation
- **Windows 10/11**: `multidestination-studio-1.1.6-windows-x64-installer.exe` (not code-signed: SmartScreen may ask for confirmation).
- **macOS**: universal `.pkg` (not signed: System Settings → Privacy & Security → Open Anyway).
- **Ubuntu 24.04**: `multidestination-studio-1.1.6-x86_64-linux-gnu.deb` (`sudo apt install ./multidestination-studio-1.1.6-x86_64-linux-gnu.deb`).

Close OBS before installing. Requires OBS Studio 31.1 or later.
