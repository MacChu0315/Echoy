# Clock mapping before connection timeout (2026-10-15)

The first v3 package `7c6e37e7be4d` passed separated startup verification, but the
user then reports reproducible persistent recovery when attaching a right tile.
Read-only official queries confirm the same three expected ELF/Boot IDs, zero
application crashes and prior successful magnetic calibration. Current owner
snapshots capture recovery on f40c6c/f45936; after a user rejoin, recovery is also
captured on f452a0. A brief non-reproduction report is followed by renewed stable
reproduction; it is not functional acceptance.

## Cause and narrow change

`START` carries the root's absolute monotonic deadline. A newly admitted follower
used `now - root_offset >= deadline` even before clock mapping existed, when
`root_offset` was still zero. Physical application boots differ by roughly 70
seconds; a follower whose uptime leads its root by more than the 30-second
attempt budget therefore immediately aborts a fresh transaction. Continuous
healthy heartbeats do not authorize indefinite retries, leaving recovery visible.

The old independent harness used only 0/200/400 ms offsets and missed this case.
Changing only offsets to 0/70/140 seconds reproduces immediate PREPARING→BROKEN:
see `large-offset-red.log`. The permanent regression also fails before the fix:
`boot-offsets-red.log`.

The coordinator now keeps a separate local monotonic deadline while mapping is
unknown. Once mapped, it uses the original root deadline in the translated
timeline. A superseding edit sharing that root deadline cannot extend the local
bound; a new group initializes its own budget. No extra retry, longer timeout,
weaker clock validation, synthesis/UI change or wire-format change is introduced.

## Verification

`sh tests/group/run_three.sh boot-offsets` passes for all six assignments of
0/70/140-second boot clocks, both join orders, A/B/C-first with actual PCM, and
both signs of root/follower offset. With PING, PONG and ABORT dropped, new edits
still cannot prevent the local 30-second timeout. Full suites include this test.
See `boot-offsets-fixed.log`, `host-results.json` and `sanitizer-results.json`.
Six separate pairs also remain connected beyond the 30-second budget, accept a
prompt right-side first Play and align a later left-side start. Normal and
sanitizer checks pass; the same two-device assertion fails against the original
code (`boot-offsets-pairs-red.log`).

All 23 normal command groups and eight ASan/UBSan/LeakSanitizer groups pass,
including original two-device cases, shared arrangement/audio checks and all
51 runtime cases. The SDK bundle builds with the known retained-factory size
warning; its application destination remains the larger retained OTA partition.
`source-hashes.json` records the tested source and is checked after each suite.

Independent review found no substantive issue in local budget initialization,
supersession, new-group reset or negative clock offsets; the reviewer also ran
the targeted regression successfully. Official native normal/reduced-motion
checks now use actual independent clocks offset by 0/70/140 seconds, not the old
millisecond-only adapter. Both native modes pass seven groups each with zero
late blocks/clipping and exact paused silence; see `native-normal/` and `native-reduced/`. Immutable candidate
`ba951963754a`, source `a83a64e`, ELF
`cc151795ec3efd59c639ce09b613f480ca601660f14a60cb1acd2639a6e1b711` is recorded
in `candidate.json`; its partition/UI bytes match accepted components.

## Evidence boundaries

Device snapshots are read-only metadata from existing ECA1 diagnostics; there
was no playback input, microphone capture, reboot or firmware write during
diagnosis. Live Gateway log replay includes historical boots, so retained link
logs explicitly filter to each verified current Boot ID. Historical battery,
calibration and audio errors are not assigned to this application boot.

The accepted dual tag/package and sound `0953c52` remain preserved. The user
subsequently approved this exact fix package on replacement f45936 and freshly
confirmed all three separated >=15 cm on a nonmetal surface. The
original two retain standing authorization. One official system update per device
succeeded with HEALTHY/validated and commit-response evidence. New application
Boot IDs, identity order f452a0/f40c6c/f45936:
`9463761514545310676` / `17925284075629591042` / `12347754192822218685`.
Component/ELF, retained bootloader/partition/UI, actual home, current-boot magnetic
calibration and zero app-crash/output-error checks pass on all three. See
`device-results.json` and each device directory. The known Recovery 0.1.2 startup
watchdog recurred on the original two; stable ready boots and preserved crash
metadata precede each single update. Recovery 0.1.4 on the third had zero crashes.
No Recovery/bootloader replacement or microphone/reference-tone operation.

The user repeated the original failing pair/order and reports
"显示跟随左侧，保持稳定". Fresh read-only evidence identifies f45936 on the left,
f452a0 on the right. The right's own Boot log records LEFT contact at 294191 ms;
at uptime 340967482 us (46.776 seconds later) it has a committed valid phrase,
FOLLOWING=4, no pending/failed preparation and a real "跟随左侧" screenshot.
The left is CONNECTED=1. Both are locally paused in the snapshot; the third is
solo. All three retain their expected new Boot/ELF with zero application crashes,
clipping, partial writes, write errors and deadline misses. Right uptime is
roughly 180 seconds ahead of left, exercising the actual large-offset condition.

This confirms the reported right-attachment regression within that pair/order;
see `physical-regression.json` and the `*-validation-*` snapshots/screens. Full
three-board interaction/listening, acoustic phase precision and sustained RF/load
acceptance remain pending. Completed third-package permission is not blanket
authorization for future writes.
