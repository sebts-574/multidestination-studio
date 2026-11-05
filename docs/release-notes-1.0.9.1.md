# Multidestination Studio 1.0.9.1

Test build: changes since 1.0.9.

## Transitions
- **Scene Transitions dock**: the list only switches between the existing OBS transitions; it no longer creates transitions.
- **Quick transitions**: start empty.
  - **+** only creates a new quick transition. Choose a transition type (Cut, Fade, Swipe, Slide, Stinger, Fade to Color, Luma Wipe, Move) and the duration at the bottom of the menu. Cut and Fade use the OBS built-in transitions; the other types open the OBS name and properties windows, and the new transition becomes the quick transition.
  - The arrow of a quick transition only modifies it (Properties…, duration) or removes it.
- **Fixed**: quick transitions shown as “missing transition” after restarting OBS (OBS gives its transitions a new identifier at each start; quick transitions now find them by name). Old entries that cannot be found are dropped.
- **T-bar** under the quick transitions, as in OBS: drag to the end to finish the transition on every destination, back to the start to cancel it. Cut and Stinger are replaced by a fade, as in OBS.
- The **⋮** icon is centred in its button.
