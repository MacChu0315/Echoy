# Complete-sample contract

Use project-specific dimensions, rates and bounds. Do not transplant a thermal
module's thresholds or assume that a hotter region identifies a person.

| Field | Meaning to establish |
| --- | --- |
| Epoch and sequence | Identity of a new complete source sample; reset behavior |
| Capture time | Physical/acquisition time and its documented precision |
| Receive/process time | When the application can use it; monotonic clock mapping |
| Array and validity | Units, dimensions, row/column order, per-pixel and whole-frame validity |
| Result | Target/no-target, data waiting/valid/invalid/stale, position validity, last-valid time |

A timestamp of zero may be valid; keep a separate “has valid sample” flag when
necessary. Validate finite values and bounds before processing. Document whether
an invalid sample consumes a source identity and how failures age prior data.
Samples, faults and time-only ticks are different trace events.

## Ownership example

```c
/* Pseudocode: resolve real APIs from the target project. */
if (driver_read_new_complete(&raw)) {
    frame = convert_complete(raw, ++sequence, capture_time);
    source_submit(&source, &frame, receive_time);
} else {
    source_fault(&source, receive_time); /* never resubmit old raw pixels */
}
source_update_behavior(&source, receive_time);
publish_latest_display_copy(&source); /* may drop older DISPLAY copies */

/* On the UI owner: advance this copy only; do not feed it into source. */
copy = receive_latest_or_keep_previous_copy();
age_and_animate(&copy, render_time);
prepare_pose(&copy);
draw_prepared_pose();
```

The actual wrapper must preserve result aging and failure semantics. Trace the
counts at driver → processor → display boundaries. If the detector requires
several continuous samples, verify it still receives them while drawing is much
slower than sampling. Optional telemetry copies coherent state and formats it
after releasing shared locks; queries must not become extra observations.

## Coordinates and recordings

Keep raw array and detector coordinates in the source convention. Apply physical
mount rotation/flips once at the output mapping; exercise both axes and preserve
mapping on replay reset. Direction acceptance requires observing a known moving
source; a native transform test is not a physical orientation measurement.

For recordings retain original arrays and fault events, capture/receive times,
sequence identity, acquisition configuration, format/version, source hash,
scene description and missing labels. A text conversion must retain precision
(e.g. a verified float roundtrip) and event order. Keep immutable originals and
regenerate adapters; do not hand-edit a few coordinates into a success demo.
Replay transport scheduling can differ from source time; document both.
