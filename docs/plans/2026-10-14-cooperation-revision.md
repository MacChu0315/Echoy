# Approved cooperation revision implementation plan

Goal: implement the 2026-10-14 approved rules in `docs/PROJECT_STATE.md`, preserving `0953c52` and accepted device sound.

Architecture: physical membership controls following and clock ownership independently of retained music. The shared C band schedules prompt first playback when no group participant is playing. Session-scoped clock/status messages coordinate either side's first start without granting remote playback. Native GSP uses a centered Play target in the existing 244x84 strip.

Constraints: retain frozen v4 files, explicit local Play permission, recording/reply deferred joins, existing transaction deadlines and Recovery. Keep `CONFIG_ECHOY_OFFICE_VOICING_TRIAL=y`; no microphone capture or remote writes. Continue the existing task branch, preserving unrelated `.vscode/` files.

1. Add failing regression cases for centered follower controls, immediate detach during pending work, retained PCM, and prompt independent replay. Implement membership/clock release and strip changes. Run strip, band, deferred and linked-owner tests.
2. Add real two-owner/transport/PCM tests for left/right first start, later entry, simultaneous requests, repeated pause/start, stale packets after physical release and radio-only recovery. Implement session-scoped bidirectional clock coordination. Run transport and runtime suites plus sanitizers.
3. Build a separate official simulator. Verify actual native pointer targets, captions, centered Play, deferred recording, independent replay and shared-C PCM. Check frozen hashes and unchanged accepted sound sources/configuration.
4. Build with the pinned SDK and office defaults. Inspect package/layout/identity, review the diff, then install on the two authorized IDs with official `mosaico.py`. Fresh separated placement is confirmed by the user. Verify HEALTHY identity, retained Recovery, calibration, real home, zero startup crashes/output faults. Record evidence and remaining physical acceptance; commit only task files.

Review focus: released membership must not return through a stale queued update; radio recovery must retain physically joined semantics; clock decisions must not stomp a local pending first start; queued starts and immutable jobs must retain their cancellation fences; no clock exchange may unpause a peer.
