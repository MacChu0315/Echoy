# Bounded magnetic/link observation candidate

This is preparation for diagnosing the unresolved live BCA rearrangement, not a
claimed fix. Reattaching only A while B—C stays still still fails by user report.
Current-Boot logs show C's RIGHT release/contact observations, but existing
firmware does not expose A's sensor/filter/contact state. A portable test of the
same three actual owners transitioning ABC→BCA succeeds; it substitutes external
contacts/radio and cannot establish the physical cause (`group-reorder-existing.log`).

Source `bec0ffb` extends existing default-off `CONFIG_ECHOY_MAGNET_DIAGNOSTICS`.
It reuses successful BMM150 reads on the link worker, never the PCM/UI owner.
At most ninety valid records are emitted, no more than one per second. Normal
builds retain the disabled flag. Startup failure stays closed; no retries,
threshold/transport/musical/UI change or new sensor reads are introduced.

`raw_L/raw_R` mean the compensated BMM150 API outputs, not ADC register values.
`filtered_L/filtered_R` average the classifier's public current sample buffers;
the pinned classifier uses these same averages. `edges` is its classification,
`stable` is the debounced physical mask, `reported` is the last successful SDK
contact report (before this iteration's report call), `peers` are admitted seam
IDs, and `group` is count/rank/phase. Samples must be interpreted together.

Enabled/disabled unit tests first fail for missing live records, then pass with
the valid-sample, rate, cap and failed-read checks. ASan/UBSan/LeakSanitizer,
active-adapter tests, full three-owner suite, independent native adapter and all
51 device-runtime checks pass. Independent review found no substantive issue
and separately ran both diagnostic unit profiles successfully. The original
native GSP normal/reduced artifacts have identical binary/bundle/API hashes;
their unaffected UI/controller/PCM evidence is retained, not rerun or relabeled.

Pinned SDK bundle builds in separate `output/device-reorder-diagnostics-build`
with the known retained-factory size warning. `candidate.json` identifies the
immutable `1eb36e7d795a` bundle and ELF, the retained layout/UI component hashes
and exact verified `ba951963754a` restore package. Generated configuration differs
only in the magnetic diagnostic flag; office voicing is enabled and microphone/
reference diagnostics remain disabled. No Recovery/bootloader component exists.

Intended diagnostic target is only A/Pulse `4553502d49524953010030eda0f45936`.
The user subsequently approved this diagnostic/restore and granted standing
authorization for later Echoy firmware updates on all three active devices;
per-package approval is no longer needed. No diagnostic write has occurred at
candidate preparation. The other two retain their running verified firmware.
Actual separated nonmetal placement must still be established before restart
after the devices have been handled.
