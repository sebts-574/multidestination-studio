# Multidestination Studio 1.0.6

## Fixed
- **Multi-selection in the Scenes and Sources lists.** Ctrl + click (Cmd + click on macOS) adds or removes one scene or source; Shift + click selects everything between two rows. The selection is no longer lost when the Preview or the selected source changes.
- **Moving scenes and sources with the mouse is reliable.** Rows are no longer lost or misplaced when several are dragged, or when a folder and its scenes are dragged together. A scene can be dropped into a folder from above or below it. Dropping just under an open folder puts the scene at the top of that folder. A folder dropped into another folder is placed after it.

## New
- **Group into a folder** (Scenes): select several scenes or folders and click the folder button (or right-click → *Group into a folder*). Scenes of selected folders join the new folder.
- **Group button** in the Sources dock (Ctrl+G), and *Group selected sources* / *Ungroup* in the right-click menu.
- **Drag sources into and out of groups**, as in OBS.
- **Visibility (eye) and lock icons** on each source, as in OBS. A locked source cannot be moved with the mouse.
- **Rename in place**: F2 (Enter on macOS) or right-click → *Rename*, for scenes, folders, sources and groups.

## Improved
- Scenes and Sources docks closer to OBS: drop indicator, folders and groups open automatically while dragging over them, uniform toolbar buttons, smooth scrolling, scroll position kept when the list refreshes.

## Known limitations
- Moving sources into or out of a group, grouping and ungrouping cannot be undone with Ctrl+Z.
