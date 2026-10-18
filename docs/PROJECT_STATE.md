# Echoy project state

The v1.1.0 source includes the three-device horizontal-link repair, shared Flash
calibration, earlier local recording replies, corrected guide and center audition.
See [release details](releases/v1.1.0.md) for behavior, package identity and checks,
and [development](development.md) for the pinned environment and test commands.

The v1.1.0 source is merged into `master`; the source tag remains unchanged.
The [GitHub Release](https://github.com/MacChu0315/Echoy/releases/tag/v1.1.0)
provides the accepted firmware, checksums, installation guide and license
materials. README download links point to this release. The older v1.0.0
package remains available as a historical version.

Detailed device snapshots, experiment logs and local workspace notes for this
release are retained privately. Existing published verification records remain
historical and do not describe the current release unless explicitly referenced.
The accepted firmware is unchanged by publication cleanup. Unmerged experimental
features are excluded. Archive branches are frozen, not ongoing state journals.


## Unreleased phrase pickup integration

The auditioned opening behavior is integrated on the v1.1.0 source baseline.
Pickup notes are prepared only for local playback and artwork; recorded input,
relationship plans, upstream transport and fallback remain unchanged. Original
PCM is preserved from the first input onward with the same office voicing.

The v1.1.0 early local reply keeps its original timing and uses pickup metadata
from its actual playback bank, including before group commit or after a failed
transaction. Random audition, input limiting and role/image guards are retained.
Shared-C, retained-baseline PCM, runtime, UI and native three-owner regression
checks pass. Physical listening of the integrated version remains pending; the
published v1.1.0 package and its acceptance are unchanged. See
[development](development.md#phrase-pickup-regression) for the original-mode switch
and reproducible checks. Private run artifacts are not part of this Git tree.
