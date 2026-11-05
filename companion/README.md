# Companion presets for Multidestination Studio

Changes to the Bitfocus Companion **OBS Studio** module (`bitfocus/companion-module-obs-studio`)
that add a **Multidestination Studio** preset section when the plugin is installed in OBS.

- `UPSTREAM_COMMIT`: the upstream commit the patch applies to.
- `multidestination-studio.patch`: the changes (new actions, feedbacks, presets; help and changelog).
- The GitHub workflow `Companion module` applies the patch, runs the module's checks and builds the
  module package (`.tgz`), available as a workflow artifact.

Install in Companion: Modules → Import module package → choose the `.tgz`, then use it for the OBS
connection. Until the change is accepted upstream, this build replaces the official OBS module.
