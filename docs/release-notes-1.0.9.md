# Multidestination Studio 1.0.9

## New

### Companion: Multidestination Studio presets
- With the patched Companion **OBS Studio** module, the OBS connection detects the plugin and adds a **Multidestination Studio** preset section:
  - **Scene to Preview** and **Scene to Program**: one button per scene, green in Preview, red in Program.
  - **Transition**, and **Transition With**: one button per OBS transition.
  - **After a Transition** and **Destinations Shown**: toggles, lit when on.
  - **Cue to Preview** and **Stop Cue**, when Cues are on.
- Matching actions and feedbacks under "Multidestination Studio". The buttons follow the studio live (new `StateChanged` vendor event).
- New vendor requests: `SetAfterTransition`, `SetDestinationsShown`. `GetState` also returns both settings and the cue in Preview / running.
- The module package (`obs-studio-4.0.0-multidestination.1.tgz`) is built by the repository's **Companion module** workflow. Import it in Companion (Modules → Import module package).

### Cues
- **Next at media end**: a checkbox per scene. The next scene starts when the media (video or audio) of the scene ends, instead of after the post-wait.
- **Live status** in the Cues dock: pre-wait countdown, post-wait countdown, or media time (0:12 / 0:45) on the running scene. The running cue opens by itself.

### Transitions between Preview and Program, as in OBS
- **Transition** button with its **⋮** menu (Previous Program returns to Preview, Show all destinations).
- The **Cut** button is gone: Cut is a quick transition you add if you want it.
- **Quick transitions** with **+**: pick a transition and a duration at the bottom of the menu. The buttons stack under Transition; the arrow on each button opens its menu (Remove, change transition, duration).

## Improved
- **Cue editor**: the transition list shows **Default** and every OBS transition. The duration follows the transition, as in the OBS dock: Default → default duration, not editable; Cut → greyed; other transitions → editable, 300 ms proposed.
- **Scenes, Sources and Cues docks**: the open/close arrow of folders, groups and cues is inside the row; no more separate block on the left of selected rows, and items outside folders are no longer shifted.
- **Folder button** with a line icon matching the OBS theme. In the Sources dock it creates a new folder (OBS group), with the selected sources in it, and opens its name for editing.

## Removed
- **Configuration Settings → Companion / Stream Deck** (follow the OBS Preview / Program scenes). The Companion presets now drive the studio directly. A scene changed in OBS acts again only on the default destination, as before 1.0.8.
