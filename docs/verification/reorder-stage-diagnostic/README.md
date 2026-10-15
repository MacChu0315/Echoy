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

Initial candidate 21bc020ef620 installed/HEALTHY on B only (operation
fcf4d266-ae90-4ff2-afd3-11aa56a1b35e, boot15480745818673229062). The
postcheck rejected its long diagnostic line: Iris chunks at 240 bytes, so
fields were split across records, not lost. Complete new group records are
now separate short lines, covered by a length/count/disabled regression.
Revised candidate 90f5098a2b4f is built and inspected in `split/`, awaiting
installation on all three. B currently shows its RIGHT committed to C/RIGHT
and group1/no admitted peer, confirming the generic rotated-session gap. Existing normal
f9b87dd4d4d7 is retained for diagnostic restoration; startup separation is not
required. Installation on all three is covered by standing authorization.
Preserve B—C—A during acquisition. Actual failure cause beyond the corrected
magnetic threshold remains open; do not mark the full three-device test passed.

## Current handoff

Revised diagnostic90f5098a2b4f installed/HEALTHY on B only, operation
be791898-9d63-4c88-95fa-1b8eb2c67833, boot1897294828789155138. Its separate
group lines are complete. B remains group1/rank0/attempt0 with no admitted peer
after the invalid same-edge session. C/A were never installed with either
diagnostic candidate in this investigation.

The complementary-only temporary probe passes5/6 static permutations; order1
pairs endpoints across the middle. Its printed `committed_same_edge` counter
also includes wrong-neighbor complementary pairs: inspect the node/peer IDs,
not that label, for the shortcut case. This is not production code.

Further diagnostic rollout was stopped after this evidence. B is restored to
normal f9b87dd4d4d7, operation a4912e78-a132-4dce-b1e5-d73e39205936,
HEALTHY boot13772919324190082093. Current-boot sampling, zero app/output errors,
no live diagnostics, and retained components are verified. C/A remain on the
same normal image; their existing phrases were not deliberately reset here.
No functional pairing/transport repair has been applied. Test5 remains failed.

A proposed larger application-owned neighbor admission/transport repair is in
`../../plans/2026-10-15-horizontal-admission-repair.md`. It distinguishes known
local evidence from unseen-peer ambiguity and leaves the latter as a required
correctness condition. It changes the handshake compatibility boundary, so the
user's instruction to review material architecture/compatibility changes applies.
Standing firmware installation authorization is unchanged.
