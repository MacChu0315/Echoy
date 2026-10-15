# Horizontal neighbor admission repair — approved 2026-10-15

## Goal and retained behavior

Support at most three same-orientation Mosaicos: separated, any pair, and any
three-node horizontal order. Recognize physical detach immediately after the
existing sensor confirmation, preserve phrases/local playing permissions, and
make ABC→BCA converge without restart or a required user pause between moves.
Preserve 0953c52 office sound, frozen UI, shared Flash profile600, eight-sample
filter, retained Recovery and the accepted dual rollback. No SDK/BSP edits,
new dependencies, microphone acquisition or remote writes.

## Evidence and limits

- Recorded saturated left Z620..623 was rejected by650; shared600 corrects it
  while the pinned560 false-left fixture remains rejected. That fix is delivered.
- Staged tests 1–4 pass: separated software restart, solo recording, AB silent
  following, then ABC silent following. Test5 fails after fast ABC→BCA movement;
  a second attachment shows connected, but readback subsequently finds C/A in
  recovery with phrases retained and no app crash.
- The generic BSP commits same-edge pairs at180 degrees. Echoy rejects those
  neighbors only after SDK allocation. This is observed physically and
  reproduced with unmodified actual SDK session/topology sources.
- A throwaway restriction to complementary edges succeeds in five tested static
  orderings, but one still pairs the two endpoints across the middle. Thus
  direction filtering alone is not a complete fix. Probe ordering is deterministic;
  it is not an exhaustive packet-scheduling experiment.
- Repeated ESPNOW_NO_MEM sends are also observed, especially on the middle node.
  They do not by themselves distinguish heap pressure, transmit backlog, or RF
  delivery behavior. This proposal must measure those separately; do not claim
  neighbor selection explains every observed recovery failure.

## Recommended change

Move horizontal neighbor admission into application-owned portable code, using
public `mosaico_peer_link` radio APIs and the pinned classifier. Replace the
application's use of unconstrained generic network auto-pairing; do not patch,
copy, or fork SDK internals. Keep the musical `echoy_group` owner/protocol and
its bounded preparation/commit rules.

Each participant advertises the complete debounced left/right contact mask,
identity, boot nonce and monotonically increasing contact generation. Contact
mask0 is still advertised so an unrelated third device does not join a pair.
A proposed seam binds both endpoints, complementary edges and their exact
contact generations, and requires reciprocal confirmation before music admission.
Messages from an old generation or boot cannot establish or revive a seam.

For a complete fresh scene view, accept only the unique physically compatible
horizontal graph: R+L for a pair (any remaining known tile reports0), or
R+LR+L for a three-node chain. With an LR middle, endpoints must select its
corresponding side, not each other. Do not use RSSI or radio proximity as contact.
Do not hardcode the three device IDs, assume their role names, or infer a
physical release from radio timeout.

During movement, retain known peer/contact generations until fresh evidence
resolves them; revoke a local released seam immediately, cancel outstanding
proposals, and reject conflicting/partial new graphs. Existing correctly bound
seams can survive radio-only interruption according to the approved rules.
User-visible policy: uncertain new adjacency stays unadmitted/recovering rather
than choosing a convenient radio peer; no new controls or manual pairing steps.
A required fixed three-second user wait is not an acceptable repair.

Important boundary: binary local magnetic masks cannot prove which unseen radio
unit is physically between endpoints under arbitrary packet loss. A missing
previously observed participant must not be silently treated as contact-free to
force convergence. The implementation must make scene-view freshness and
membership explicit and test missing/late advertisements. If reliable automatic
admission requires a new persistent roster/manual enrollment or changes the
user-visible treatment of an unavailable third unit, return that specific
product choice for review rather than inventing it.

Use bounded application-owned addressed transport for admitted neighbors and
one intermediate hop where needed. Preserve existing group payloads and
completion callbacks. Fence routing/acknowledgements with the admitted graph
and boot/contact generations; cap inflight operations and retries, preserve
control capacity, and measure send pressure on the middle device. Reuse public
portable utilities where their contracts fit, without copying generic network
implementation. Do not fix NO_MEM by extending transaction deadlines or blindly
increasing retry counts.

## Compatibility and scope

The new admission/transport capability must be explicitly versioned and refuse
cooperation with older packages that cannot confirm the same graph. The actual
musical phrase format remains unchanged. Update all three to one verified new
version for acceptance; retain f9b87dd4d4d7 normal and the accepted dual baseline
for deliberate rollback. No persistent-data migration is proposed.

The user approved this architecture/compatibility change with “同意推进” on
2026-10-15. Implementation and verification are now in progress. Standing
installation authorization remains valid; no repeated package approval is needed.

## Implementation and acceptance sequence

1. Preserve current failed hardware evidence and create executable regressions
   for the same-edge and endpoint-shortcut probes using actual public SDK code.
2. Implement and test portable contact-generation discovery and reciprocal
   neighbor selection, including all six orders, simultaneous/staggered joins,
   fast reorder, stale/duplicate/lost frames, reboot and missing participants.
   No musical owner receives an unconfirmed or incompatible seam.
3. Integrate bounded direct/one-hop transport with the existing actual three
   owners and PCM scheduler. Exercise delayed/failed completions, route changes,
   middle forwarding pressure, old group packets, pause and retained phrases.
4. Run existing regression commands and matching official native simulator
   checks (normal/reduced motion as applicable); verify sound/UI/source hashes,
   SDK build and final package/component identities.
5. Install through official workflows under standing authorization. Repeat
   independent startup, AB, ABC, fast ABC→BCA, all six orders, detach retention,
   and joined startup. Collect boot/contact/neighbor/owner/transport evidence.
   Do not call the work complete while the current test5 path still fails or
   unexplained repeated radio-send failures prevent stable operation.
