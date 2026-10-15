# Live ABC → BCA reordering observation

On installed fix `ba951963754a`, the user reports A no longer follows C after
moving ABC into BCA. They confirm the present left-to-right role order is
Harmony, Voice, Pulse. Device labels therefore map to B=f452a0, C=f40c6c,
A=f45936; they are human labels, not fixed musical roots.

Expected: B is the leftmost root, C follows B and A follows C. Existing local
roles and Play/Pause permission remain local. Joining may defer during capture
or one-shot reply, but both read-only observations show all three idle, with no
pending/failed preparation. B reports CONNECTED=1, C FOLLOWING=4 and A SOLO=0.
All three retain the expected ELF/Boot IDs with zero crashes. The live chain is
recognized as a pair plus an independent tile, so checklist item 3 has failed
for this transition. The user's earlier basic-three report remains scoped to
that initial successful layout.

`observation.json`, real screenshots and current-Boot logs preserve the scene.
Official `mosaico.py` device-status/logs/existing ECA1 metadata/screenshot calls
are read-only; no restart, firmware write or production code change occurred.
No automated tests were newly run in this observation-only step.

Cause remains unresolved. Current evidence does not distinguish missing physical
contact from radio admission or a stale previous relationship, and does not
establish a hardware defect. Retain the failed BCA scene for follow-up diagnosis;
do not substitute restarting it for observing the transition.
