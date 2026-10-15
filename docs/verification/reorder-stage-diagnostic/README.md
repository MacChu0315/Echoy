# Rearrangement recovery investigation

The user reports test 5 failed: quickly moving A from ABC left to BC right
was not recognized; removing it again, pausing and rejoining showed connected.
Read-only same-boot snapshots show B root/cooperation=1, but C/A following with
cooperation=5 (recovery); A requested revision14 versus committed12. Phrases
remain present, paused, and no preparation fault or app crash is reported.
Subsequent 60-second official log collection retains repeated ESPNOW NO_MEM
sends (501 matching lines for middle C, 41 A, 15 B; these counts include paired
error/warning lines and are not unique failed packets). No inferred hardware
memory cause or new network repair is claimed from this error alone.

A default-off bounded diagnostic now adds existing group identity, attempt,
operation count, three member IDs, mapped offset and deadline to the existing
once-per-second/90-record sensor/edge/phase log. It uses the link worker's
existing read-only info API. No protocol, retry, threshold, synthesis, UI or
playback behavior changes. Diagnostic enabled/disabled checks, real-BSP contact
regression, adapter and ASan/UBSan/LSan pass. Official SDK build succeeds
(run 20261015-170143-system-update-bundle-796578, known factory-size warning).
Package/source/component identities are in diagnostic-candidate.json and
source-hashes.json. Native/PCM sources match the previously verified normal
and reduced-motion artifacts; no new simulator run is claimed.

Candidate 21bc020ef620 is prepared, not yet installed. Existing normal
f9b87dd4d4d7 is retained for diagnostic restoration; startup separation is not
required. Installation on all three is covered by standing authorization.
Preserve B—C—A during acquisition. Actual failure cause beyond the corrected
magnetic threshold remains open; do not mark the full three-device test passed.
