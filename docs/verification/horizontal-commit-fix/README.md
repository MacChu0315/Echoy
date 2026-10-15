# Routed commit fairness and preparation budget

Current hardware0606 retains correct ABC adjacency and zero raw submit failures,
but B/C recovered after initial following and C failed preparation. User role
selection order is unknown. Actual source seed6768 has six touches, A Pulse13
events, B Harmony/C Voice12 events. Read-only ECA1 captures are retained under
output/group-commit-diagnostic-20261015/plan-{A,B,C}; no microphone recording.

Low-index ACK/relay slots could repeatedly overtake older pending slots. Each
queue now rotates independently, retaining ACK priority and relay/local class
alternation. Both new queue tests fail against the old loops and pass with the
fix. The real radio/group/band/PCM pressure test covers both role-before-record
and role-after-record with captured seed6768, differing boot clocks,20ms simulated
MAC completion and1.8s worker completion. Those delays are a stress profile,
not a claim about measured live RF latency.

With fair queues but the old one-hop lead, the later-role case still misses its
common boundary. Reserve one second per path hop before choosing ARM: pairs1s,
triples2s. The locked bar is never moved,30s attempt limit and200ms authorization
guard remain, as do prompt first Play and per-device permission. This can choose
one later musical bar for a three-node update. No wire/data migration or audio
change. Retain the first fault per attempt before ABORT, including original
clock/deadline, owner readiness and member flags in short periodic log records.
Historical faults must be compared with the current identity/attempt.

Red/green evidence: output/group-commit-diagnostic-20261015/{relay-fairness-red,
ack-fairness-red,queue-fairness-green,hop-budget-red,hop-budget-green}.log.
Independent read-only review found no Important/Critical issue. Full26 host
suite and radio/pressure/adapter ASan/UBSan/LSan pass. The pinned SDK build
20261015-194644-system-update-bundle-937989 passes with only the known retained
factory-size warning; the application fits its OTA partition. Matching official
native normal/reduced each pass7/7 with original10ms clock tolerance, zero late
PCM blocks/clipping and exact paused silence. Evidence is retained in
output/horizontal-commit-fix-20261015/.
The immutable candidate is recorded in normal-candidate.json. Hardware delivery
and original rapid ABC→BCA acceptance remain pending; no physical success is
inferred from simulator results.
