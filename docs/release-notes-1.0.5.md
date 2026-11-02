# Multidestination Studio 1.0.5

## New
- **Scene folders.** Organise the Scenes list in collapsible folders: **New folder** button, right-click → *New folder*, *Rename folder*, *Remove folder*. Selected scenes move into a new folder; drag scenes in and out of folders. Folders only organise the list: a folder is never taken to Program. Removing a folder keeps its scenes. Folders are saved with the scene collection.
- **Source groups**, as in OBS. **Edit → Group selected sources** (Ctrl+G), **Ungroup** (Ctrl+Shift+G), right-click → *Rename group*. Groups can be collapsed in the Sources list; selecting a source inside a group edits that source.

## Changed
- **macOS: Multidestination Studio opens in a large window** instead of full screen. Windows and Linux still open full screen. F11 still switches to full screen.

## Fixed
- **Output screens stay open and on top** of their screen on Windows, macOS and Linux, whatever you do in OBS or in the studio. Opening an output no longer takes the focus from the studio. If an output screen is disconnected, its output hides and comes back by itself when the screen returns. (An output opened on the screen OBS is on is not kept on top, so it never hides OBS.)
- **Enter no longer closes Output blending or Manage destinations.** Enter confirms the value you typed and the window stays open; it only presses a button when that button has the focus.
- **macOS: closing a window no longer sends OBS to the background.** When a dialog, floating dock or output closes, the studio takes the focus back.

## Known limitations
- Grouping and ungrouping cannot be undone with Ctrl+Z.
- Sources inside a group are reordered in OBS; in the Sources list, only top-level sources and groups can be dragged.
