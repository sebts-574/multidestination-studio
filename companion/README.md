# Companion presets for Multidestination Studio

Changes to the Bitfocus Companion **OBS Studio** module (`bitfocus/companion-module-obs-studio`)
that add a **Multidestination Studio** preset section when the plugin is installed in OBS.

- `UPSTREAM_COMMIT`: the upstream commit the patch applies to.
- `MODULE_VERSION`: the version given to the built module.
- `multidestination-studio.patch`: the changes (actions, feedbacks, presets, tests, help, changelog).
- `RELEASE.md`: installation notes shown on the release page.

The GitHub workflow **Companion module** applies the patch, runs the module's checks (format, lint,
types, tests) and publishes the module package as a `.tgz` on the **companion-module** release of
this repository. Users download that `.tgz` and import it in Companion (Modules → Import module
package).
