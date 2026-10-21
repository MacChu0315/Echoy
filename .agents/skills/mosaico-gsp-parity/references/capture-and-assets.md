# Native capture, deterministic assets and effective configuration

## Native state and pixels

Use the real shared C backend, matching generated bindings/resources and official
native simulator. A browser-only animation or scene-only render does not verify
application handlers, sensor processing or role timing. Debug controls belong
outside an approved full-screen artwork when they are not product UI.

A control acknowledgement may precede a committed frame. For a controlled
checkpoint, pause logical time, reach the state, wait for the simulator's
observable commit/frame completion, then capture pixels and state. Use its
supported bounded readiness API; a guessed sleep or cached screenshot can pair
an old frame with a new pose. Test failure should first distinguish that capture
race from a renderer mismatch.

Dynamic evidence retains original screenshot wall timestamps. Encode the movie
with those intervals; fixed-rate encoding can speed up a busy screenshot loop
and falsely shorten a dwell or wake. Verify movie duration against the observed
wall interval. Retain capture metadata and label sampling limits. Equivalent
pixels for a pose do not establish correct temporal progression.

For physical screenshots use the target project's official device CLI and retain
identity/Boot/artifact metadata; inspect the saved image. A Gateway web page is
not a substitute source of device pixels. Simulator success and physical
orientation/timing/appearance acceptance remain separate.

## Ordinary asset builds are read-only

Pin any conversion version needed for byte identity, preserve source/provenance
hashes, and generate only into a build tree. Ordinary builds validate retained
inputs; source/provenance regeneration is a separate explicit operation. A
missing original is not permission to re-create an approved visual from memory.

Test failure ordering deliberately: a missing-original test must reach the
missing-original condition independently of an unrelated imaging-library version.
Test the pinned-version rejection separately. Compare source hashes and mtimes
across an ordinary build; a passing renderer does not excuse metadata churn.
Do not copy full original review archives or third-party checkouts into a small
package when retained sources/provenance are sufficient.

## Effective build state

Read the actual ESP-IDF revision and dependency constraints; development Git
labels and CMake versions can differ. Do not replace an unsupported target or
upgrade shared SDK/BSP/GSP checkouts to make a build pass. Resolve official
scaffolding, pins, generated files and integration from the current project.

Defaults are not the effective sdkconfig. Assert required view, version, input
mapping and resource settings in the generated configuration/artifacts. A normal
incremental build can retain old selectors; trigger the documented project-local
reconfiguration and rerun assertions. Do not install an artifact whose selectors
were rejected, silently edit generated files, or fullclean by habit.
Keep the exact build error and successful reproduction distinct.
