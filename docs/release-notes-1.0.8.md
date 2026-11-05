# Multidestination Studio 1.0.8

## New

### Companion / Stream Deck
- **Scene to PVW**: a scene put in the OBS Preview (Companion *Set Preview Scene*, obs-websocket, the OBS scene list in Studio Mode) appears in the studio Preview. The studio Preview is also sent back to OBS, so Companion's *Scene in Preview* feedback stays correct.
- **Scene to PGM**: a scene put on air in OBS (Companion *Set Program Scene* or *Transition*) is taken by every unlocked destination covered by the scene, with the OBS transition.
- **Companion presets work as they are**: scene buttons, *Set Transition Type* and *Set Transition Duration* from the Companion OBS connection drive the studio.
- **obs-websocket vendor requests** (Companion *Custom Vendor Request*, vendor `multidestination-studio`): `GetState`, `SetPreviewScene`, `SetProgramScene`, `Transition`, `Cut`, `SetPreviewCue`, `GoCue`, `StopCue`. Data fields: `sceneName`, `cueName`, `transitionName`, `duration`, `cut`. Events: `PreviewSceneChanged`, `ProgramSceneChanged`.
- **Configuration Settings → Companion / Stream Deck…**: switches to follow the OBS Preview and Program scenes (both on by default), with setup instructions.

### Cues (optional, off by default)
- **Configuration Settings → Cues… → Activate Cues** shows the new **Cues** dock, placed between Sources and Media controls.
- A cue plays several scenes one after the other. Each scene has a **pre-wait**, its own **transition** and **duration**, and a **post-wait**.
- Click a cue to show its first scene in Preview, then press **Transition** or **Cut** to run it. The running cue and scene are marked ▶. Run and Stop buttons in the dock.
- New cues start from the scenes selected in the Scenes dock. Cues are saved with the scene collection.

### Transitions
- **Every OBS transition type** (Swipe, Slide, Stinger, Fade to Color, Luma Wipe, Move…) can be added from the transition list of the Scene Transitions dock. The transition is created in OBS, with its name and properties windows, and can be used everywhere.
- **Remove** button for the current transition.
- **Quick transitions** between Preview and Program, as in OBS Studio Mode: click **+** to add one (any transition, any duration), click it to take the Preview with that transition. Right-click to change its transition or duration, or remove it. Quick transitions are saved with the scene collection.

## Improved
- **Scenes and Sources docks look like OBS**: toolbar with the OBS theme icons, rounded rows, source type icons, OBS eye and lock icons, OBS group icon for folders and groups.
- **Scene filters** button in the Scenes dock.

## Changed
- **Configuration Settings menu**: After a transition, Destinations shown, Companion / Stream Deck, then Cues (between separators), then Startup Settings and Language.
- **Changes made in OBS now reach every destination** when *Follow the OBS Program scene* is on (default). Turn it off in Companion / Stream Deck to keep the previous behaviour (only the default destination follows OBS).

## Known limitations
- Companion *Set Preview Scene* and *Transition* need OBS Studio Mode, which is on while Multidestination Studio is open.
- Quick transitions have no hotkeys yet.
- A quick transition or cue that uses another transition than the current one switches the OBS transition for the time of the transition, then switches it back.
